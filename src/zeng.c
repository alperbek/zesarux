/*
    ZEsarUX  ZX Second-Emulator And Released for UniX
    Copyright (C) 2013 Cesar Hernandez Bano

    This file is part of ZEsarUX.

    ZEsarUX is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>



#include "cpu.h"
#include "debug.h"
#include "utils.h"
#include "network.h"
#include "compileoptions.h"
#include "zeng.h"
#include "remote.h"
#include "snap_zsf.h"
#include "autoselectoptions.h"



#ifdef USE_PTHREADS

#include <pthread.h>
#include <sys/types.h>


pthread_t thread_zeng;

#endif

//Si el thread se ha inicializado correctamente
//z80_bit thread_zeng_inicializado={0};



//-ZENG: ZEsarUX Network Gaming



//La fifo

zeng_key_presses zeng_key_presses_array[ZENG_FIFO_SIZE];

int zeng_remote_socket=-1;


//Tamanyo de la fifo
int zeng_fifo_current_size=0;

//Posicion de agregar de la fifo
int zeng_fifo_write_position=0;

//Posicion de leer de la fifo
int zeng_fifo_read_position=0;

//Si esta habilitado zeng
z80_bit zeng_enabled={0};

//Hostname remoto
char zeng_remote_hostname[MAX_ZENG_HOSTNAME]="";

//Puerto remoto
int zeng_remote_port=10000;

int segundos_cada_snapshot=2;

int zeng_i_am_master=0;

//Mensaje de envio a footer remoto
//Con margen +100 de sobra para agregar el comando print-footer
char zeng_send_message_footer[AUTOSELECTOPTIONS_MAX_FOOTER_LENGTH+100];

int pending_zeng_send_message_footer=0;


int zeng_next_position(int pos)
{
	pos++; 
	if (pos==ZENG_FIFO_SIZE) pos=0;
	return pos;
}

//Agregar elemento a la fifo
//Retorna 1 si esta llena
int zeng_fifo_add_element(zeng_key_presses *elemento)
{
	//Si esta llena, no hacer nada
	//TODO: esperar a flush
	//TODO: semaforo

	if (zeng_fifo_current_size==ZENG_FIFO_SIZE) return 1;

	//Escribir en la posicion actual
	zeng_key_presses_array[zeng_fifo_write_position].tecla=elemento->tecla;
	zeng_key_presses_array[zeng_fifo_write_position].pressrelease=elemento->pressrelease;

	//Y poner siguiente posicion
	zeng_fifo_write_position=zeng_next_position(zeng_fifo_write_position);

	//Y sumar total elementos
	zeng_fifo_current_size++;

	return 0;

}

//Leer elemento de la fifo
//Retorna 1 si esta vacia
int zeng_fifo_read_element(zeng_key_presses *elemento)
{
	//TODO: semaforo

	if (zeng_fifo_current_size==0) return 1;

	//Leer de la posicion actual
	elemento->tecla=zeng_key_presses_array[zeng_fifo_read_position].tecla;
	elemento->pressrelease=zeng_key_presses_array[zeng_fifo_read_position].pressrelease;

	//Y poner siguiente posicion
	zeng_fifo_read_position=zeng_next_position(zeng_fifo_read_position);

	//Y restar total elementos
	zeng_fifo_current_size--;

	return 0;

}

void zeng_empty_fifo(void)
{
	//Tamanyo de la fifo
	zeng_fifo_current_size=0;

	//Posicion de agregar de la fifo
	zeng_fifo_write_position=0;

	//Posicion de leer de la fifo
	zeng_fifo_read_position=0;
}


void zeng_send_key_event(enum util_teclas tecla,int pressrelease)
{
	if (zeng_enabled.v==0) return;

	//Si esta menu abierto, tampoco enviar
	if (menu_abierto) return;

	//teclas F no enviar
	switch (tecla) {
		case UTIL_KEY_F1:
		case UTIL_KEY_F2:
		case UTIL_KEY_F3:
		case UTIL_KEY_F4:
		case UTIL_KEY_F5:
		case UTIL_KEY_F6:
		case UTIL_KEY_F7:
		case UTIL_KEY_F8:
		case UTIL_KEY_F9:
		case UTIL_KEY_F10:
		case UTIL_KEY_F11:
		case UTIL_KEY_F12:
		case UTIL_KEY_F13:
		case UTIL_KEY_F14:
		case UTIL_KEY_F15:
			return;
		break;

		default:
		break;
	}

	zeng_key_presses elemento;

	elemento.tecla=tecla;
	elemento.pressrelease=pressrelease;

	//printf ("Adding zeng key event to fifo\n");

	if (zeng_fifo_add_element(&elemento)) {
		debug_printf (VERBOSE_DEBUG,"Error adding zeng key event. FIFO full");
		return;
	}

}

//Devuelve 0 si no conectado
int zeng_connect_remote(void)
{

		//Inicialmente desconectado
		zeng_remote_socket=-1;

		int indice_socket=z_sock_open_connection(zeng_remote_hostname,zeng_remote_port);

		if (indice_socket<0) {
			debug_printf(VERBOSE_ERR,"ERROR. Can't create TCP socket");
			return 0;
		}

		 int posicion_command;
		
		//Leer algo
		char buffer[200];

		//int leidos=z_sock_read(indice_socket,buffer,199);
		int leidos=zsock_read_all_until_command(indice_socket,(z80_byte *)buffer,199,&posicion_command);
		if (leidos>0) {
			buffer[leidos]=0; //fin de texto
			//printf("Received text (length: %d):\n[\n%s\n]\n",leidos,buffer);
		}

		//zsock_wait_until_command_prompt(indice_socket);

		printf("Sending get-version\n");

		//Enviar un get-version
		z_sock_write_string(indice_socket,"get-version\n");

 
		//reintentar
		leidos=zsock_read_all_until_command(indice_socket,(z80_byte *)buffer,199,&posicion_command);
		if (leidos>0) {
			buffer[leidos]=0; //fin de texto
			printf("Received text for get-version (length %d): \n[\n%s\n]\n",leidos,buffer);
		}		

		//1 mas para eliminar el salto de linea anterior a "command>"
		if (posicion_command>=1) {
			buffer[posicion_command-1]=0;
			printf ("Recibida version: %s\n",buffer);
		}
		else {
			debug_printf (VERBOSE_ERR,"Error receiving ZEsarUX remote version");
			return 0;
		}

		//Comprobar que version remota sea como local
		if (strcasecmp(EMULATOR_VERSION,buffer)) {
			debug_printf (VERBOSE_ERR,"Local and remote ZEsarUX versions do not match");
			return 0;
		}

		//escribir_socket(misocket,"Waiting until command prompt final");
		//printf("Waiting until command prompt final\n");

		//zsock_wait_until_command_prompt(indice_socket);

	zeng_remote_socket=indice_socket;

	return 1;
}

//Devuelve 0 si error
int zeng_disconnect_remote(void)
{
	z_sock_close_connection(zeng_remote_socket);
	return 1;
}

int contador_envio_snapshot=0;



int zeng_send_snapshot_pending=0;


//zona memoria donde se guarda ya el snapshot con comando put-snapshot y valores hexadecimales
char *zeng_send_snapshot_mem_hexa=NULL; //zeng_send_snapshot_mem_hexa

void zeng_send_snapshot(int socket)
{
	//Enviar snapshot cada 20*250=5000 ms->5 segundos
		printf ("Enviando snapshot\n");

		int posicion_command;

				
			
				printf ("Sending put-snapshot\n");
				z_sock_write_string(socket,"put-snapshot ");
			

				//TODO esto es ineficiente y que tiene que calcular la longitud. hacer otra z_sock_write sin tener que calcular
				z_sock_write_string(socket,zeng_send_snapshot_mem_hexa);

				free(zeng_send_snapshot_mem_hexa);
				zeng_send_snapshot_mem_hexa=NULL;

			

				z80_byte buffer[200];
				//Leer hasta prompt
				 int leidos=zsock_read_all_until_command(socket,buffer,199,&posicion_command);

		
}



void *thread_zeng_function(void *nada)
{
	/*
Hilo de sincronización de juego:

-si flag de envío de snapshot, se envía. Ese flag lo activa el core al final de frame, cada X segundos, y cuando somos el máster

-si hay que enviar mensaje al otro jugador, enviarlo

-ver la fifo usada en envío de eventos:
*tecla
*press/release

Dicha fifo hay que controlarla mediante semáforos
Se mete elementos en fifo cuando se llama a util send press/release
Se leen y envían eventos de la fifo desde este thread 

-dormir durante 10ms - mitad de frame 

Para las rutinas zsock también haría falta semáforos pero como no voy a llamarla desde dos sitios distintos a la vez pues..

Poder enviar mensajes a otros jugadores 	
	 */


	int escritos;

	//TODO: controlar otros errores de envio de snapshot y mensaje. Al igual que se hace con send-keys

	while (1) {
		usleep(5000); //dormir 5 ms



		//Si hay tecla pendiente de enviar
		zeng_key_presses elemento;
		while (!zeng_fifo_read_element(&elemento) ) {
			//printf ("leido evento de la zeng fifo tecla %d pressrelease %d\n",elemento.tecla,elemento.pressrelease);

			//command> help send-keys-event
			//Syntax: send-keys-event key event

				//printf ("longitud: %d\n",longitud);
				char buffer_comando[256];
				sprintf(buffer_comando,"send-keys-event %d %d\n",elemento.tecla,elemento.pressrelease);

				escritos=z_sock_write_string(zeng_remote_socket,buffer_comando);


				//printf ("despues de enviar send-keys. escritos en write string: %d\n",escritos);

				
				//Si ha habido error al escribir en socket
				if (escritos<0) {
					debug_printf (VERBOSE_ERR,"Error sending to socket. Disabling zeng");

					//Aqui cerramos el thread desde mismo dentro del thread
					zeng_disable_forced();


				}

				else {

					z80_byte buffer[200];

					//Leer hasta prompt
					int posicion_command;

					//printf ("antes de leer hasta command prompt\n");
					int leidos=zsock_read_all_until_command(zeng_remote_socket,buffer,199,&posicion_command);

					//printf ("despues de leer hasta command prompt\n");
				}

		}



		//Si hay mensaje pendiente de enviar
		if (pending_zeng_send_message_footer) {
			z_sock_write_string(zeng_remote_socket,zeng_send_message_footer);

			//Leer hasta prompt
			int posicion_command;
			z80_byte buffer[200];
			int leidos=zsock_read_all_until_command(zeng_remote_socket,buffer,199,&posicion_command);
			
			pending_zeng_send_message_footer=0;

		}



		//Si hay snapshot pendiente de enviar
		if (zeng_i_am_master) {
			if (zeng_send_snapshot_pending && zeng_send_snapshot_mem_hexa!=NULL) {
				zeng_send_snapshot(zeng_remote_socket);
				zeng_send_snapshot_pending=0;
			}
		}
	}
}




void zeng_send_snapshot_if_needed(void)
{

	if (zeng_enabled.v==0) return;

	if (zeng_i_am_master) {
		contador_envio_snapshot++;
		//printf ("%d %d\n",contador_envio_snapshot,(contador_envio_snapshot % (50*segundos_cada_snapshot) ));
		if ( (contador_envio_snapshot % (50*segundos_cada_snapshot) )==0) { //cada 5 segundos
				//Si esta el anterior snapshot aun pendiente de enviar
				if (zeng_send_snapshot_pending) {
					printf ("Anterior snapshot aun no se ha enviado\n");
				}
				else {
					//zona de memoria donde se guarda el snapshot pero sin pasar a hexa
					z80_byte *buffer_temp;
					buffer_temp=malloc(ZRCP_GET_PUT_SNAPSHOT_MEM); //16 MB es mas que suficiente

					if (buffer_temp==NULL) cpu_panic("Can not allocate memory for get-snapshot");

					int longitud;

  					save_zsf_snapshot_file_mem(NULL,buffer_temp,&longitud);	

								

					zeng_send_snapshot_mem_hexa=malloc(ZRCP_GET_PUT_SNAPSHOT_MEM*2); //16 MB es mas que suficiente

					int char_destino=0;

					int i;
		
					for (i=0;i<longitud;i++,char_destino +=2) {
						sprintf (&zeng_send_snapshot_mem_hexa[char_destino],"%02X",buffer_temp[i]);
					}

					//metemos salto de linea y 0 al final
					strcpy (&zeng_send_snapshot_mem_hexa[char_destino],"\n");

					printf ("Poniendo en cola snapshot para enviar snapshot longitud %d\n",longitud);


					//Liberar memoria que ya no se usa
					free(buffer_temp);


					zeng_send_snapshot_pending=1;


				}
		}
	}
}

void zeng_enable(void)
{

	//ya  inicializado
	if (zeng_enabled.v) return;

	if (zeng_remote_hostname[0]==0) return;

#ifdef USE_PTHREADS

	//No hay pendiente snapshot
	zeng_send_snapshot_pending=0;

	//Conectar a remoto
	if (!zeng_connect_remote()) {
		//Desconectar solo si el socket estaba conectado
		if (zeng_remote_socket>=0) zeng_disconnect_remote();
		return;
	}


	//Inicializar thread

	//thread_zeng_inicializado.v=0;

	if (pthread_create( &thread_zeng, NULL, &thread_zeng_function, NULL) ) {
		debug_printf(VERBOSE_ERR,"Can not create zeng pthread");
		return;
	}


	//thread_zeng_inicializado.v=1;


	zeng_enabled.v=1;
#else
	//sin threads
	zeng_enabled.v=0;
#endif



}

//desactivar zeng sin cerrar socket pues ha fallado la conexion


void zeng_disable_normal(int forced)
{

	//ya  cerrado
	if (zeng_enabled.v==0) return;


#ifdef USE_PTHREADS

	zeng_enabled.v=0;


	//Finalizar thread
	//printf ("antes de pthread_cancel\n");

	pthread_cancel(thread_zeng);

	//printf ("despues de pthread_cancel\n");


	//Vaciar fifo
	zeng_empty_fifo();

	//Decir que no hay snapshot pendiente
	zeng_send_snapshot_pending=0;

	//Cerrar conexión con ZRCP.
	if (!forced) {
		//TODO: enviarle un "quit"
		zeng_disconnect_remote();
	}
	else {
		//Liberar socket z_sock pero sin desconectarlo realmente
		z_sock_free_connection(zeng_remote_socket);
	}

#else
	//sin threads
	zeng_enabled.v=0;
#endif


}

void zeng_disable(void)
{
	zeng_disable_normal(0);
}

void zeng_disable_forced(void)
{
	zeng_disable_normal(1);
}

void zeng_add_pending_send_message_footer(char *mensaje)
{

	if (!pending_zeng_send_message_footer) {
		sprintf(zeng_send_message_footer,"print-footer %s\n",mensaje);
		pending_zeng_send_message_footer=1;
		printf ("Poniendo en cola enviar mensaje a remoto: %s\n",mensaje);
	}

}
