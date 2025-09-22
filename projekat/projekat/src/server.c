/*
    ********************************************************************
    Odsek:          Elektrotehnika i racunarstvo
    Departman:      Racunarstvo i automatika
    Katedra:        Racunarska tehnika i racunarske komunikacije (RT-RK)
    Predmet:        Osnovi Racunarskih Mreza
    Godina studija: Treca (III)
    Skolska godina: 2021/22
    Semestar:       Zimski (V)

    Ime fajla:      server.c
    Opis:           UDP server

    Platforma:      Raspberry Pi 2 - Model B
    OS:             Raspbian
    ********************************************************************
*/
//TODO: staviti printf za svake rp_pack slucajeve
//	staviti rp_pack malloc u while-u i free(rp_pack) na kraju svakog slucaja

#include <stdio.h>      //printf, perror
#include <stdlib.h>     //EXIT_FAILURE, EXIT_SUCCESS
#include <string.h>     //memset
#include <sys/socket.h> //socket, bind, recvfrom, sendto
#include <arpa/inet.h>  //struct sockaddr_in
#include <unistd.h>     //close
#include <pthread.h>
#include <semaphore.h>
#include <limits.h>

#include <errno.h>

#include "err_codes.h"
#include "packet_types.h" //tp i rp paketi
#include "timerfd.h" //za periodicne niti
#include "tools.h"
#include "lists.h"

#define IP_ADDRESS     "127.0.0.1"
#define PORT           5001
#define DEFAULT_BUFLEN 20

#define PACKET_SIZE    81
#define NEIGHBOUR_SIZE 2 //odredjuje koliko rutera moze biti povezano

int server_socket_fd, router_fd;
struct sockaddr_in servaddr, cliaddr, router_addr, rt_cliaddr;
char srv_addr[IP_LEN] = "000.001";
char* actual_ips[NEIGHBOUR_SIZE]; //prave ip adrese sa zapravo slanje poruka

//stvari sa rt prefiksom su namenjene rp protokol funkcijama (rp_read_packets i rp_parser)
static sem_t semNotEmpty, rtNotEmpty;
static pthread_mutex_t buff_mux, rt_buff_mux, table_mux;//, port_mux;
TP_Packet* buffer[DEFAULT_BUFLEN];
RP_Packet* rt_buff[DEFAULT_BUFLEN]; 
int rp_head_ind, head_ind;
//posto vise threadova koristi ove bafere, onda su ovo globalne vrednosti  

void tp_read_buffer(TP_Packet* packet, int* tail_ind) 
{
    strcpy(packet->dest, buffer[*tail_ind]->dest);
    strcpy(packet->src, buffer[*tail_ind]->src);
    packet->cmd = buffer[*tail_ind]->cmd;
    strcpy(packet->msg, buffer[*tail_ind]->msg);
    *tail_ind = (*tail_ind + 1) % DEFAULT_BUFLEN;
}

void tp_feed_buffer(TP_Packet* tp_pack, int* head_ind) 
{
    strcpy(buffer[*head_ind]->dest, tp_pack->dest);
    strcpy(buffer[*head_ind]->src, tp_pack->src);
    buffer[*head_ind]->cmd = tp_pack->cmd;
    strcpy(buffer[*head_ind]->msg, tp_pack->msg);
    *head_ind = (*head_ind + 1) % DEFAULT_BUFLEN;
}

void rp_read_buffer(RP_Packet* rp_pack, int* tail_ind) 
{
    strcpy(rp_pack->final_dest, rt_buff[*tail_ind]->final_dest);
    strcpy(rp_pack->curr_src, rt_buff[*tail_ind]->curr_src);
    strcpy(rp_pack->start_src, rt_buff[*tail_ind]->start_src);
    rp_pack->cmd = rt_buff[*tail_ind]->cmd;
    rp_pack->hop_cnt = rt_buff[*tail_ind]->hop_cnt;
    rp_pack->tp_pkt = rt_buff[*tail_ind]->tp_pkt;
    *tail_ind = (*tail_ind + 1) % DEFAULT_BUFLEN;
}

void rp_feed_buffer(RP_Packet* rp_pack, int* rp_head_ind) 
{
    strcpy(rt_buff[*rp_head_ind]->final_dest, rp_pack->final_dest);
    strcpy(rt_buff[*rp_head_ind]->curr_src, rp_pack->curr_src);
    strcpy(rt_buff[*rp_head_ind]->start_src, rp_pack->start_src);
    rt_buff[*rp_head_ind]->cmd = rp_pack->cmd;
    rt_buff[*rp_head_ind]->hop_cnt = rp_pack->hop_cnt;
    rt_buff[*rp_head_ind]->tp_pkt = rp_pack->tp_pkt;
    *rp_head_ind = (*rp_head_ind + 1) % DEFAULT_BUFLEN;
}

RP_Packet* rp_generate_packet(TP_Packet* tp_pkt, char* dest) 
{
    RP_Packet* rp_pack = malloc(sizeof(RP_Packet));
    strcpy(rp_pack->final_dest, dest);
    strcpy(rp_pack->curr_src, srv_addr);
    strcpy(rp_pack->start_src, srv_addr);
    rp_pack->cmd = 's';
    rp_pack->hop_cnt = 0;
    rp_pack->tp_pkt = *tp_pkt;
    return rp_pack;
}

void* refresh_table(void* param)
{
    Router_Table** neighbour_heads = (Router_Table **)param;
    periodic_info p_info;
    
    //20s period
    make_periodic(20000000, &p_info, 0);
    
    wait_period(&p_info); //ceka 20s pre nego sto zapocne zapravo
    while (1)
    {
    	for (int i = 0; i < NEIGHBOUR_SIZE; i++)
    	{
    	    printf("Removing elem from list\n");
    	    pthread_mutex_lock(&table_mux);
    	    rt_remove_node(&neighbour_heads[i]);
    	    pthread_mutex_unlock(&table_mux);
    	}
    	    
    	wait_period(&p_info);
    }
}

void* broadcast(void* param)
{
    int send_fd;
    struct sockaddr_in send_addr;
    periodic_info p_info;
    ssize_t sent_size;
    
    //RP_Packet* rp_pack = malloc(sizeof(RP_Packet));
    
    if ((send_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
    	perror("Socket creation failed");
    	return 0;
    }
    else
    {   	    	   	
    	//10s period
    	make_periodic(10000000, &p_info, 1);
    	while (1)
    	{
    	    RP_Packet* rp_pack = malloc(sizeof(RP_Packet));
    	    strcpy(rp_pack->final_dest, "000.000");
    	    strcpy(rp_pack->curr_src, srv_addr);
    	    strcpy(rp_pack->start_src, srv_addr);
    	    rp_pack->cmd = 'b';
    	    rp_pack->hop_cnt = 1;
    	
    	    strcpy(rp_pack->tp_pkt.dest, "\0");
    	    strcpy(rp_pack->tp_pkt.src, "\0");
    	    rp_pack->tp_pkt.cmd = 0;
    	    strcpy(rp_pack->tp_pkt.msg, "\0");
    	    
    	    memset(&send_addr, 0, sizeof(send_addr));
    	    send_addr.sin_family = AF_INET;
    	    send_addr.sin_port = htons(PORT+2000);
    	    
    	    for (int i = 0; i < NEIGHBOUR_SIZE; i++)
    	    {
    	    	if (strcmp(actual_ips[i], "000.000.000.000") == 0)
    	    	    continue;
    	    	
    	    	//pthread_mutex_lock(&port_mux);
    	    	send_addr.sin_addr.s_addr = inet_addr(actual_ips[i]);   	    	    	    	
    	    	if ((sent_size = sendto(send_fd, (const RP_Packet *)rp_pack, sizeof(RP_Packet), 0, (const struct sockaddr *)&send_addr, sizeof(send_addr))) == sizeof(RP_Packet))
    	    	{
    	    	    printf("Sent message size %ld bytes\n", sent_size);
    	    	    printf("Sent message to %s:%hu\n", inet_ntoa(send_addr.sin_addr), htons(send_addr.sin_port));
    	    	}
    	    	else
    	    	{
    	    	    printf("Router not responding, removing from list\n");
    	    	    strcpy(actual_ips[i], "000.000.000.000");
    	    	}
    	    	//pthread_mutex_unlock(&port_mux); 
    	    }
    	    free(rp_pack);
    	    wait_period(&p_info);
    	}
    }
    
    return 0;
}

void* tp_parser(void* param)
{
    MAIL_LIST* head = (MAIL_LIST*)param;

    char *ip;
    ssize_t sent_size;
    
    int tail_ind = 0;
    char stat_msg;
    
    while (1)
    {
    	TP_Packet* packet = malloc(sizeof(TP_Packet));
    	//proverava da li ima nesto u baferu
        if (sem_trywait(&semNotEmpty) == 0)
        {
            pthread_mutex_lock(&buff_mux);
            tp_read_buffer(packet, &tail_ind);
            pthread_mutex_unlock(&buff_mux);
            
            //uzima prvi oktet adrese da proveri da li se poruka salje lokalno ili na neki drugi ruter
            char dest_cmp[4], srv_cmp[4];
            strncpy(dest_cmp, packet->dest, 3);
            strncpy(srv_cmp, srv_addr, 3);

            if (!strcmp(srv_cmp, dest_cmp))
            {
                switch(packet->cmd)
                {
                    case 'o':
                        ip = packet->src;
                        printf("%s\n", ip);
                        add_node(&head, ip);
            
                        stat_msg = 'a';                   
                        if ((sent_size = sendto(server_socket_fd, (const char *)&stat_msg, strlen(&stat_msg), 0, (const struct sockaddr *)&cliaddr, sizeof(cliaddr))) == strlen(&stat_msg))
                        {
                            printf("Message sent\n");
                        }
                        else
                        {
                            perror("send failed");
                            printf("%d\n", errno);
                            EXIT_FAILURE;
                        }
                        break;
            
                    case 's':
                        struct sockaddr_in recv_addr;
                        memset(&recv_addr, 0, sizeof(recv_addr));
                
                        ip = packet->dest;       
                        if (find_node(&head, ip) == NULL)
                        {
                            printf("Address %s was not found in the list\n", ip);
                            stat_msg = NO_POST;
                        }
                        else
                        {
                            stat_msg = 'a';
                            char* msg_ptr = malloc(MSG_LEN);
                            strcpy(msg_ptr, packet->msg);
                    
                            int recv_port = get_port(ip) + 1000;
                            recv_addr.sin_family = AF_INET;
                            recv_addr.sin_addr.s_addr = inet_addr(IP_ADDRESS);
                            recv_addr.sin_port = htons(recv_port);
                    
                            if ((sent_size = sendto(server_socket_fd, (const char *)msg_ptr, strlen(msg_ptr), 0, (const struct sockaddr *)&recv_addr, sizeof(recv_addr))) == strlen(msg_ptr))
                                    printf("Message to recv sent\n");
                            else
                            {
                                    perror("send failed");
                                    EXIT_FAILURE;
                            }            	
                
                        }
                                    
                        if ((sent_size = sendto(server_socket_fd, (const char *)&stat_msg, strlen(&stat_msg), 0, (const struct sockaddr *)&cliaddr, sizeof(cliaddr))) == strlen(&stat_msg))
                            printf("Message sent\n");
                
                        else
                        {
                            perror("send failed");
                            EXIT_FAILURE;
                        }
                        break;
                    
                    case 'c':
                        ip = packet->src;
                        remove_node(&head, ip);
                            
                        stat_msg = 'a';     
                        if ((sent_size = sendto(server_socket_fd, (const char *)&stat_msg, strlen(&stat_msg), 0, (const struct sockaddr *)&cliaddr, sizeof(cliaddr))) == strlen(&stat_msg))
                        {
                            printf("Message sent\n");
                        }
                        else
                        {
                            perror("send failed");
                            EXIT_FAILURE;
                        }
                        break;
                        
                    default:
                            break;
                }
            }
                
            else
            {
                if ((sent_size = sendto(server_socket_fd, (const char *)&stat_msg, strlen(&stat_msg), 0, (const struct sockaddr *)&cliaddr, sizeof(cliaddr))) == strlen(&stat_msg))
                {
                    printf("RP packet created\n");
                }                  
                
                char dest[8];
                make_dest(dest, packet);
                
                printf("DEST %s\n", dest);
                
                RP_Packet* rp_pack = rp_generate_packet(packet, dest);
                
                pthread_mutex_lock(&rt_buff_mux);
                rp_feed_buffer(rp_pack, &rp_head_ind);
                pthread_mutex_unlock(&rt_buff_mux); 
                sem_post(&rtNotEmpty);
                
                free(rp_pack);
            }
                
            puts("\nMail list:");
            output(head);
        } 
    	
        free(packet);   	
    }
    
    return 0;
}

void* rp_parser(void* param)
{
    Router_Table** neighbour_heads = (Router_Table**)param;
    
    int tail_ind = 0;
    
    ssize_t sent_size;
    struct sockaddr_in send_addr;
    int send_fd;

    if ((send_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        return 0;

    memset(&send_addr, 0, sizeof(send_addr));
    send_addr.sin_family = AF_INET;
    send_addr.sin_port = htons(PORT+2000);
    
    //niz should_send da se zna od kog suseda je najmanja putanja do finalnog (preko hop count)
    int should_send[NEIGHBOUR_SIZE];         
    
    while (1)
    {
    	for (int i = 0; i < NEIGHBOUR_SIZE; i++)
    	    should_send[i] = INT_MAX;
    	    
    	RP_Packet* rp_pack = malloc(sizeof(RP_Packet));
    	if (sem_trywait(&rtNotEmpty) == 0)
    	{
    	    pthread_mutex_lock(&rt_buff_mux);
            rp_read_buffer(rp_pack, &tail_ind);
            pthread_mutex_unlock(&rt_buff_mux);
            
            //moze i provera sa start_src jer gleda da li od njega polazi rp poruka
            if (rp_pack->hop_cnt == 0)
            {
            	printf("New packet from the router with cmd %c\n", rp_pack->cmd);
            	rp_pack->hop_cnt++;
            	Router_Table* tmp;
            	for (int i = 0; i < NEIGHBOUR_SIZE; i++)
            	{
            	    pthread_mutex_lock(&table_mux);
            	    if ((tmp = rt_find_node(&neighbour_heads[i], rp_pack->final_dest)) != NULL)
            	    	should_send[i] = tmp->hopcount;

            	    pthread_mutex_unlock(&table_mux);
            	}

                int mini = INT_MAX;
                int mini_idx;
                for(int j = 0; j < NEIGHBOUR_SIZE; j++)
                {
                    if(should_send[j] < mini)
                    {
                        mini = should_send[j];
                        mini_idx = j;
                    }
                }

                //pthread_mutex_lock(&port_mux);
                printf("Should send index: %d %s\n", should_send[mini_idx], actual_ips[mini_idx]);
                send_addr.sin_addr.s_addr = inet_addr(actual_ips[mini_idx]);
                
                if ((sent_size = sendto(send_fd, (const RP_Packet*)rp_pack, sizeof(RP_Packet), 0, (const struct sockaddr *)&send_addr, sizeof(send_addr))) == sizeof(RP_Packet))
                {
                    printf("Sent message size %ld bytes\n", sent_size);
    	    	    printf("Sent message to %s:%hu\n", inet_ntoa(send_addr.sin_addr), htons(send_addr.sin_port));
                }
                //pthread_mutex_unlock(&port_mux);         	
            }
            else if (rp_pack->cmd == 'b')
            {
                if (strcmp(rp_pack->start_src, srv_addr) != 0)
                {                    
                    int i;
                    for (i = 0; i < NEIGHBOUR_SIZE; i++)
                    {
                    	pthread_mutex_lock(&table_mux);
                    	if (strcmp(neighbour_heads[i]->ip, rp_pack->curr_src) == 0)
                    	{
                    	    pthread_mutex_unlock(&table_mux);
                    	    break;
                    	}
                    	pthread_mutex_unlock(&table_mux);
                    }
                      
                    int exists = 0;
                    for (int j = 0; j < NEIGHBOUR_SIZE; j++)
                    {
                    	pthread_mutex_lock(&table_mux);
                        if (rt_find_node(&neighbour_heads[j], rp_pack->start_src) != NULL)
                        {
                            exists = 1;
                            pthread_mutex_unlock(&table_mux);
                            break;
                        }
                        pthread_mutex_unlock(&table_mux);
                    }
                    if (!exists)
                    {
                        printf("Adding node %s %d\n", rp_pack->start_src, rp_pack->hop_cnt);
                        pthread_mutex_lock(&table_mux);
                        rt_add_node(&neighbour_heads[i], rp_pack->start_src, rp_pack->hop_cnt);
                        rt_output(neighbour_heads[i]);
                        pthread_mutex_unlock(&table_mux);
                    }
                    else
                        printf("%s already exists in some list\n", rp_pack->start_src);

                    rp_pack->hop_cnt++;
                    strcpy(rp_pack->curr_src, srv_addr);
                    for (int j = 0; j < NEIGHBOUR_SIZE; j++)
                    {
                        if (j == i)
                            continue;
                        if (strcmp(actual_ips[i], "000.000.000.000") == 0)
                            continue;

			            //pthread_mutex_lock(&port_mux);
                        send_addr.sin_addr.s_addr = inet_addr(actual_ips[j]);
                        
                        if ((sent_size = sendto(send_fd, (const RP_Packet*)rp_pack, sizeof(RP_Packet), 0, (const struct sockaddr *)&send_addr, sizeof(send_addr))) == sizeof(RP_Packet))
                        {
                            printf("Sent message size %ld bytes\n", sent_size);
    	    	            printf("Broadcast shared to %s:%hu\n", inet_ntoa(send_addr.sin_addr), htons(send_addr.sin_port));
                        }
                        //pthread_mutex_unlock(&port_mux);
                    }
                }
            }
            //ako se poruka zavrsava kod njega
            else if (strcmp(rp_pack->final_dest, srv_addr) == 0)
            {
            	printf("Packet reached its final dest\n");           	
            	TP_Packet* tp_pack = &(rp_pack->tp_pkt);
            	
            	pthread_mutex_lock(&buff_mux);
            	tp_feed_buffer(tp_pack, &head_ind);
            	pthread_mutex_unlock(&buff_mux); 
            	sem_post(&semNotEmpty);
            }
            //ni jedno, ni drugo, ni trece - intermediary u prenosu
            else
            {
            	printf("Packet in an intermediary router\n");
                int i;
                for (i = 0; i < NEIGHBOUR_SIZE; i++)
                {
                    if (strcmp(neighbour_heads[i]->ip, rp_pack->curr_src) == 0)
                    break;
                }  
                
                rp_pack->hop_cnt++;
                strcpy(rp_pack->curr_src, srv_addr);
                
            	Router_Table* tmp;
            	for (int j = 0; j < NEIGHBOUR_SIZE; j++)
            	{
            	    if (j == i)
            	    	continue;
            	    	
            	    pthread_mutex_lock(&table_mux);
            	    if ((tmp = rt_find_node(&neighbour_heads[j], rp_pack->final_dest)) != NULL)
            	    	should_send[j] = tmp->hopcount;
            	    pthread_mutex_unlock(&table_mux);
            	}
            	    	
                int mini = INT_MAX;
                int mini_idx;
                for(int j = 0; j < NEIGHBOUR_SIZE; j++)
                {
                    if (j == i)
                    	continue;
                    if(should_send[j] < mini)
                    {
                        mini = should_send[j];
                        mini_idx = j;
                    }
                }

		        //pthread_mutex_lock(&port_mux);
		        printf("Should send intermediary index %d %s\n", should_send[mini_idx], actual_ips[mini_idx]); 
                send_addr.sin_addr.s_addr = inet_addr(actual_ips[mini_idx]);
                
                if ((sent_size = sendto(send_fd, (const RP_Packet*)rp_pack, sizeof(RP_Packet), 0, (const struct sockaddr *)&send_addr, sizeof(send_addr))) == sizeof(RP_Packet))
                {
                    printf("Sent message size %ld bytes\n", sent_size);
    	    	    printf("Sent message to %s:%hu\n", inet_ntoa(send_addr.sin_addr), htons(send_addr.sin_port));
                }
                //pthread_mutex_unlock(&port_mux);                          	
            }           
    	}
    	free(rp_pack);
    }
    
    return 0;
}

void* tp_read_packets(void* param)
{
    ssize_t received_size;
    socklen_t len;
    len = sizeof(cliaddr);
    
    while (1)
    {
    	TP_Packet* packet = malloc(sizeof(TP_Packet));
        if ((received_size = recvfrom(server_socket_fd, (TP_Packet *)packet, sizeof(TP_Packet), 0, (struct sockaddr *)&cliaddr, &len)) >= 0)
        {
            printf("Received message: %s\n", packet->msg);
            printf("Received message size: %ld bytes\n", received_size);
            printf("Message received from (IP_ADDRESS:PORT) %s:%hu\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));                    
            
            pthread_mutex_lock(&buff_mux);
            tp_feed_buffer(packet, &head_ind);
            pthread_mutex_unlock(&buff_mux); 
            sem_post(&semNotEmpty);     
        }
        else
        {
            perror("receive failed");
            break;
        }
        free(packet);
     }
            
    return 0;
}

void* rp_read_packets(void* param)
{
    ssize_t recv_size;
    socklen_t len = sizeof(rt_cliaddr);
    
    while (1)
    {
    	RP_Packet* pack = malloc(sizeof(RP_Packet));
    	
    	if ((recv_size = recvfrom(router_fd, (RP_Packet *)pack, sizeof(RP_Packet), 0, (struct sockaddr *)&rt_cliaddr, &len)) >= 0)
        {           
            printf("Received message size: %ld bytes\n", recv_size);
            printf("Message received from (IP_ADDRESS:PORT) %s:%hu\n", inet_ntoa(rt_cliaddr.sin_addr), ntohs(rt_cliaddr.sin_port));                    
            
            pthread_mutex_lock(&rt_buff_mux);
            rp_feed_buffer(pack, &rp_head_ind);
            pthread_mutex_unlock(&rt_buff_mux); 
            sem_post(&rtNotEmpty);     
        }
        else
        {
            perror("receive failed");
            break;
        }
        free(pack);
    }
    
    return 0;
}

int main()
{
    int return_value = EXIT_SUCCESS;
    
    MAIL_LIST *head;
    init(&head);
    
    //inicijalizacija tp i rp bafera
    for (int i = 0; i < DEFAULT_BUFLEN; i++)
    	buffer[i] = malloc(sizeof(TP_Packet));
    	
    for (int i = 0; i < DEFAULT_BUFLEN; i++)
    	rt_buff[i] = malloc(sizeof(RP_Packet));   
    	
    rp_head_ind = 0;
    head_ind = 0;	 	

    pthread_t hTP_Read_Packets;
    pthread_t hTP_Parser;
    
    pthread_t hRP_Read_Packets;
    pthread_t hRP_Parser;
    
    pthread_t hBroadcast;
    pthread_t hRefresh;
    
    //unos stvarnih ip adresa za aktuelno slanje poruka
    char srv_ip[16], actual_srv_ip[16];
    printf("Enter your ip addr: ");
    fgets(srv_ip, 16, stdin);
    srv_ip[strcspn(srv_ip, "\n")] = '\0';
    strcpy(actual_srv_ip, srv_ip);
    get_srv_ip(srv_ip, srv_addr);
    printf("%s\n", srv_addr);
    
    for (int i = 0; i < NEIGHBOUR_SIZE; i++)
    	actual_ips[i] = (char *)malloc(16);
    
    char neighbour_ip[IP_LEN] = "000.001";
    
    Router_Table* neighbours[NEIGHBOUR_SIZE];   
    for (int i = 0; i < NEIGHBOUR_SIZE; i++)
    {
    	rt_init(&neighbours[i]);   
    	printf("Enter the neighbouring router's ip addr[%d]: ", i);
    	fgets(srv_ip, 16, stdin);
    	srv_ip[strcspn(srv_ip, "\n")] = '\0';
    	strcpy(actual_ips[i], srv_ip);
    	get_srv_ip(srv_ip, neighbour_ip);
    	if (strcmp(actual_ips[i], "000.000.000.000") != 0)
    	{
    	    rt_add_node(&neighbours[i], neighbour_ip, 1);
    	    rt_output(neighbours[i]);
    	}
    }
    
    // Creating socket file descriptor
    if ((server_socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket creation failed");
        return_value = EXIT_FAILURE;
    }
    else
    {
        printf("Socket created successfully, socket_fd = %d\n", server_socket_fd);
        memset(&servaddr, 0, sizeof(servaddr));
        memset(&cliaddr, 0, sizeof(cliaddr));
        memset(&router_addr, 0, sizeof(router_addr));

        // Filling server information
        servaddr.sin_family = AF_INET; // IPv4
        servaddr.sin_addr.s_addr = inet_addr(IP_ADDRESS);
        servaddr.sin_port = htons(PORT);

        // Bind the socket with the server address
        if (bind(server_socket_fd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
        {
            perror("bind failed");
            return_value = EXIT_FAILURE;
        }
        else
        {
            printf("Socket bound successfully, IP address: %s, port: %hu\n", inet_ntoa(servaddr.sin_addr), ntohs(servaddr.sin_port));
            
            if ((router_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
            {
            	perror("socket creation failed");
            	return_value = EXIT_FAILURE;
	        }
            else
            {
                printf("Socket created successfully, socket_fd = %d\n", router_fd);
                
                router_addr.sin_family = AF_INET; // IPv4
                router_addr.sin_addr.s_addr = inet_addr(actual_srv_ip);
                router_addr.sin_port = htons(PORT+2000);
                
                if (bind(router_fd, (const struct sockaddr *)&router_addr, sizeof(router_addr)) < 0)
                {
                    perror("bind failed");
                        return_value = EXIT_FAILURE;
                }
                else
                {
                    printf("Socket bound successfully, IP address: %s, port: %hu\n", inet_ntoa(router_addr.sin_addr), ntohs(router_addr.sin_port));
                    
                    sem_init(&semNotEmpty, 0, 0);
                    pthread_mutex_init(&buff_mux, NULL);
                    
                    sem_init(&rtNotEmpty, 0, 0);
                    pthread_mutex_init(&rt_buff_mux, NULL);
                    
                    //pthread_mutex_init(&port_mux, NULL);
                    
                    pthread_mutex_init(&table_mux, NULL);
            
                    pthread_create(&hTP_Parser, NULL, tp_parser, (void*)head);
                    pthread_create(&hTP_Read_Packets, NULL, tp_read_packets, 0);
                    
                    pthread_create(&hRP_Parser, NULL, rp_parser, (void*)neighbours);
                    pthread_create(&hRP_Read_Packets, NULL, rp_read_packets, 0);
            
                    pthread_create(&hBroadcast, NULL, broadcast, NULL);
                    
                    pthread_create(&hRefresh, NULL, refresh_table, (void*)neighbours);
                    
                    pthread_join(hTP_Parser, NULL);
                    pthread_join(hTP_Read_Packets, NULL);
                    
                    pthread_join(hRP_Parser, NULL);
                    pthread_join(hRP_Read_Packets, NULL);
                    
                    pthread_join(hBroadcast, NULL);
                    
                    pthread_join(hRefresh, NULL);
                    
                    pthread_mutex_destroy(&table_mux);
                    
                    //pthread_mutex_destroy(&port_mux);
            
                    sem_destroy(&rtNotEmpty);
                    pthread_mutex_destroy(&rt_buff_mux);
                    
                    sem_destroy(&semNotEmpty);
                    pthread_mutex_destroy(&buff_mux);
                }
                
                close(router_fd);	    	
            }                      
        }
		
        close(server_socket_fd);
    }
    
    //oslobadjanje stvari
    for (int i = 0; i < NEIGHBOUR_SIZE; i++)
    	free(actual_ips[i]);
    
    for (int i = 0; i < DEFAULT_BUFLEN; i++)
    	free(buffer[i]);
    	
    for (int i = 0; i < DEFAULT_BUFLEN; i++)
    	free(rt_buff[i]);    	
    	
    return return_value;
}
