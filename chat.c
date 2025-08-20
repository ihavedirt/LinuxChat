/*
 * David Baik
 */

#include <stdio.h>
#include <list.h>
#include <rtthreads.h>
#include <RttCommon.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>


#define MAX_MSG_LEN 256 /* just a reasonable vavlue for msg */


RttThreadId keyboard_input_PID, rcv_udp_datagram_PID, print_to_screen_PID, 
    send_udp_datagram_PID, schat_server_PID;
struct addrinfo s_hints, p_hints;
struct addrinfo *s_addrinfo, *p_addrinfo;
int s_sock;
char *s_name;

enum sender {
    KEYBOARD,
    RCV_UDP,
    PRINT,
    SND_UDP,
    SERVER
};

struct message {
    int sender;
    char str_msg[MAX_MSG_LEN];
    ssize_t str_msg_len;
    struct timeval time;
    char name[30]; /* just a reasonable length for name*/
};

struct udp_message {
    char str_msg[MAX_MSG_LEN];
    struct timeval time;
    char name[30];
};

RTTTHREAD keyboard_input() {
    struct message snd_msg;
    struct message rply_msg;
    unsigned int rply_msg_len = sizeof(rply_msg);

    memset(&snd_msg, 0, sizeof(snd_msg));
    snd_msg.sender = KEYBOARD;
 
    while (1) {
        snd_msg.str_msg_len = read(STDIN_FILENO, snd_msg.str_msg, 
            sizeof(snd_msg.str_msg) -1);
                
        if (snd_msg.str_msg_len > 1) {
            snd_msg.str_msg[snd_msg.str_msg_len] = '\0';
            RttSend(schat_server_PID, &snd_msg, sizeof(snd_msg), &rply_msg,
                &rply_msg_len);
        }
        else {
            RttUSleep(1);
        }
    }
}

RTTTHREAD rcv_udp_datagram() {
    struct sockaddr_storage sender_addr;
    socklen_t fromlen = sizeof(sender_addr);
    struct udp_message udp_msg;
    struct message snd_msg;
    struct message rply_msg;
    unsigned int rply_msg_len = sizeof(rply_msg);
    int udp_msg_len;

    memset(&snd_msg, 0, sizeof(snd_msg));   
    snd_msg.sender = RCV_UDP;
    
    while (1) {
        udp_msg_len = recvfrom(s_sock, &udp_msg, sizeof(udp_msg), 0, 
            (struct sockaddr *)&sender_addr, &fromlen);
        
        if (udp_msg_len > 0) {
            strcpy(snd_msg.str_msg, udp_msg.str_msg);
            snd_msg.time = udp_msg.time;
            strcpy(snd_msg.name, udp_msg.name);
            snd_msg.str_msg_len = udp_msg_len;

            RttSend(schat_server_PID, &snd_msg, sizeof(snd_msg), &rply_msg,
                &rply_msg_len);
        }
        else {
            RttUSleep(1); 
        }
    }
}

RTTTHREAD print_to_screen() {
    struct message rply_msg;
    struct message snd_msg;
    unsigned int rply_msg_len = sizeof(rply_msg);

    memset(&snd_msg, 0, sizeof(snd_msg));
    snd_msg.sender = RCV_UDP;

    while (1) {
        if (rply_msg.str_msg_len > 0) {
            printf("(%s)[%ld.%6ld] %s", rply_msg.name, rply_msg.time.tv_sec, 
                rply_msg.time.tv_usec, rply_msg.str_msg);
        }

        RttSend(schat_server_PID, &snd_msg, sizeof(snd_msg), &rply_msg,
            &rply_msg_len);
    }
}

RTTTHREAD send_udp_datagram() {
    struct message snd_msg;
    struct message rply_msg;
    unsigned int rply_msg_len = sizeof(rply_msg);
    struct udp_message udp_msg;
    
    memset(&snd_msg, 0, sizeof(snd_msg));
    snd_msg.sender = SND_UDP;

    while (1) {
        if (rply_msg.str_msg_len > 0) {
            sendto(s_sock, &udp_msg, sizeof(udp_msg) +1, 0, p_addrinfo->ai_addr,
                p_addrinfo->ai_addrlen);
        }

        RttSend(schat_server_PID, &snd_msg, sizeof(snd_msg), &rply_msg,
            &rply_msg_len);
        
        /* set udp message from reply from server */
        strcpy(udp_msg.str_msg, rply_msg.str_msg);
        gettimeofday(&udp_msg.time, NULL);
        strcpy(udp_msg.name, s_name);
    }
}

RTTTHREAD schat_server() {
    RttThreadId senderPID;
    struct message rply_msg;
    unsigned int msgLen;
    struct message rcv_msg;
    struct LIST *snd_list, *rcv_list;
    int flags;

    snd_list = ListCreate();
    rcv_list = ListCreate();

    rply_msg.sender = SERVER;

    msgLen = sizeof(rcv_msg);

    printf("------- Starting Server ------\n");    

    while (1) {
        struct message temp;
        temp.str_msg_len = 0;

        RttReceive(&senderPID, &rcv_msg, &msgLen);       
        
        /* keyboard input */
        RttReply(keyboard_input_PID, &rply_msg, 
            (unsigned int) sizeof(struct message));

        if (rcv_msg.sender == KEYBOARD && rcv_msg.str_msg_len > 1) {
            if (strcmp(rcv_msg.str_msg, "/quit\n") == 0) {
                break;
            } 
            
            temp = rcv_msg;
            ListPrepend(snd_list, &temp);
        }

        /* sending UDP */
        if (ListCount(snd_list) > 0) {
            temp = *((struct message *) ListTrim(snd_list));
            RttReply(send_udp_datagram_PID, &temp,
                (unsigned int) sizeof(struct message)); 
        }

        /* receiving UDP */
        RttReply(rcv_udp_datagram_PID, &rply_msg,
             (unsigned int) sizeof(struct message));
        
        if (rcv_msg.sender == RCV_UDP && rcv_msg.str_msg_len > 1) {
            temp = rcv_msg;
            ListPrepend(rcv_list, &temp);
        }

        /* printing to screen */
        if (ListCount(rcv_list) > 0) {
            temp = *((struct message *) ListTrim(rcv_list));
            RttReply(print_to_screen_PID, &temp,
                (unsigned int) sizeof(struct message));
        }
    }
    /* exit clean up */
    printf("Exiting...\n");

    /* set read and recvfrom to blocking */
    flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
    
    flags = fcntl(s_sock, F_GETFL, 0);
    fcntl(s_sock, F_SETFL, flags & ~O_NONBLOCK);

    RttKill(keyboard_input_PID);
    RttKill(rcv_udp_datagram_PID);
    RttKill(print_to_screen_PID);
    RttKill(send_udp_datagram_PID);
    RttExit();
}

/*
 * Usage: Self Port, Self Name, Connect Port 
 * Note: Variable names with s_ are self, p_ are partner
 */
int mainp(int argc, char *argv[]) {
    int temp, flags;
    RttSchAttr attrs;

    int port_1 = atoi(argv[1]);
    int port_2 = atoi(argv[3]);

    if (argc != 4) {
        return 1;
    }

    if (port_1 < 30001 || port_1 > 40000) {
        return 1;
    }

    if (port_2 < 30001 || port_2 > 40000) {
        return 1;
    }

    s_name = argv[2];
    printf("(main) Self Name: %s\n", s_name);

    /* self info stuff */
    memset(&s_hints, 0, sizeof(s_hints));
    s_hints.ai_family = AF_INET;
    s_hints.ai_socktype = SOCK_DGRAM;
    s_hints.ai_flags = AI_PASSIVE;
    
    temp = getaddrinfo(NULL, argv[1], &s_hints, &s_addrinfo);
    if (temp < 0) printf("error on getaddrinfo()");

    printf("(main) Self Port: %d\n", ntohs(((struct sockaddr_in *)s_addrinfo->
        ai_addr)->sin_port));


    /* partner info stuff */
    memset(&p_hints, 0, sizeof(p_hints));
    p_hints.ai_family = AF_INET;
    p_hints.ai_socktype = SOCK_DGRAM;
    p_hints.ai_flags = AI_PASSIVE;

    temp = getaddrinfo(NULL, argv[3], &p_hints, &p_addrinfo);
    if (temp < 0) printf("error on getaddrinfo()");

    printf("(main) Partner Port: %d\n", 
        ntohs(((struct sockaddr_in *)p_addrinfo->ai_addr)->sin_port));


    /* socket and binding */
    printf("(main) Creating Socket\n");
    s_sock = socket(s_addrinfo->ai_family, s_addrinfo->ai_socktype, 
        s_addrinfo->ai_protocol);
    if (s_sock < 0) printf("error on socket()"); 
 
    printf("(main) Binding Socket\n");
    temp = bind(s_sock, s_addrinfo->ai_addr, s_addrinfo->ai_addrlen);
    if (temp < 0) printf("error on bind()");


    /* set keyboard read flag to nonblocking */
    flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    /* set recvfrom flag to nonblocking */
    flags = fcntl(s_sock, F_GETFL, 0);
    fcntl(s_sock, F_SETFL, flags | O_NONBLOCK);

    /* create worker threads */
    temp = RttCreate(&schat_server_PID, (void(*)()) schat_server,
        65536, "schat_server_PID", NULL, attrs, RTTUSR);
    if (temp == RTTFAILED) perror("RttCreate");
    temp = RttCreate(&keyboard_input_PID, (void(*)()) keyboard_input, 
        65536, "keyboard_input", NULL, attrs, RTTUSR);
    if (temp == RTTFAILED) perror("RttCreate");
    temp = RttCreate(&rcv_udp_datagram_PID, (void(*)()) rcv_udp_datagram,
        65536, "rcv_udp_datagram", NULL, attrs, RTTUSR);
    if (temp == RTTFAILED) perror("RttCreate");
    temp = RttCreate(&print_to_screen_PID, (void(*)()) print_to_screen, 
        65536, "print_to_screen", NULL, attrs, RTTUSR);
    if (temp == RTTFAILED) perror("RttCreate");
    temp = RttCreate(&send_udp_datagram_PID, (void(*)()) send_udp_datagram, 
        65536, "send_udp_datagram", NULL, attrs, RTTUSR);
    if (temp == RTTFAILED) perror("RttCreate");

    printf("(main) Created all threads\n");

    return 0;
}
