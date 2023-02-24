/*
 * FILE: rdt_receiver.cc
 * DESCRIPTION: Reliable data transfer receiver.
 * NOTE: This implementation assumes there is no packet loss, corruption, or
 *       reordering.  You will need to enhance it to deal with all these
 *       situations.  In this implementation, the packet format is laid out as
 *       the following:
 *
 *       |<-  1 byte  ->|<-             the rest            ->|
 *       | payload size |<-             payload             ->|
 *
 *       The first byte of each packet indicates the size of the payload
 *       (excluding this single-byte header)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>

#include "rdt_struct.h"
#include "rdt_receiver.h"

#define HEADER_SIZE 5
#define WINDOW_SIZE 10
#define MAX_WINDOW_NUM (10 * WINDOW_SIZE)

int all=0;

// define the window
struct window
{
    int begin_num = -WINDOW_SIZE;
    packet *pkts[WINDOW_SIZE];
};

std::unique_ptr<window> receiver_pkt_window;

int Receiver_Make_Checksum(packet *pkt)
{
    // first init to zero
    int checksum = 0;
    if (pkt == NULL)
        return checksum;

    // add all the characters in payload, but jump the checksum bit
    for (int i = 1; i < RDT_PKTSIZE; i++)
        checksum += pkt->data[i];

    return checksum;
}

bool Receiver_Check_Checksum(packet *pkt)
{
    // first init to zero
    int checksum = 0;
    if (pkt == NULL)
        return false;

    // add all the characters in payload, but jump the checksum bit
    for (int i = 1; i < RDT_PKTSIZE; i++)
        checksum += pkt->data[i];

    return (char)checksum == pkt->data[0];
}

void Clean_Window()
{
    
    // move all items to msg
    int i;
    for (i = 0; i < WINDOW_SIZE; i++)
    {
        if (receiver_pkt_window->pkts[i] == NULL)
            break;

        packet *pkt = receiver_pkt_window->pkts[i];

        struct message *msg = (struct message *)malloc(sizeof(struct message));
        ASSERT(msg != NULL);

        msg->size = pkt->data[1];

        /* sanity check in case the packet is corrupted */
        if (msg->size < 0)
            msg->size = 0;
        if (msg->size > RDT_PKTSIZE - HEADER_SIZE)
            msg->size = RDT_PKTSIZE - HEADER_SIZE;

        msg->data = (char *)malloc(msg->size);
        ASSERT(msg->data != NULL);
        memcpy(msg->data, pkt->data + HEADER_SIZE, msg->size);
        Receiver_ToUpperLayer(msg);
    }
    // printf("clean:%d %d\n",receiver_pkt_window->begin_num, i);

    // clean it
    for (i = 0; i < WINDOW_SIZE; i++)
    {
        receiver_pkt_window->pkts[i] = NULL;
    }
    receiver_pkt_window->begin_num = (receiver_pkt_window->begin_num + WINDOW_SIZE) % MAX_WINDOW_NUM;
}

void Change_Window(packet *pkt)
{
    
    int seq = pkt->data[3];
    if (seq >= receiver_pkt_window->begin_num && seq < receiver_pkt_window->begin_num + WINDOW_SIZE)
    {
        // now window
        receiver_pkt_window->pkts[seq % WINDOW_SIZE] = new packet();
        memcpy(receiver_pkt_window->pkts[seq%WINDOW_SIZE]->data,pkt->data,RDT_PKTSIZE);
    }
    else if (seq >= (receiver_pkt_window->begin_num + WINDOW_SIZE) % MAX_WINDOW_NUM)
    {
        // new window
        Clean_Window();
        receiver_pkt_window->pkts[seq % WINDOW_SIZE] = new packet();
        memcpy(receiver_pkt_window->pkts[seq%WINDOW_SIZE]->data,pkt->data,RDT_PKTSIZE);
        // printf("set %d %d\n",seq % WINDOW_SIZE,seq);
        // printf("%d %d\n",pkt->data[3],receiver_pkt_window->begin_num);
    }
    else if (seq < receiver_pkt_window->begin_num)
    {
        // drop it
        printf("error drop");
        // printf("%d %d\n",pkt->data[3],receiver_pkt_window->begin_num);
    }
    // printf("%d %d\n",pkt->data[3],receiver_pkt_window->begin_num);
    
}

/* receiver initialization, called once at the very beginning */
void Receiver_Init()
{
    fprintf(stdout, "At %.2fs: receiver initializing ...\n", GetSimulationTime());
    // init pkt buffer
    receiver_pkt_window = std::make_unique<window>();
    for (int i = 0; i < WINDOW_SIZE; i++)
        receiver_pkt_window->pkts[i] = NULL;
    receiver_pkt_window->begin_num = (receiver_pkt_window->begin_num + WINDOW_SIZE) % MAX_WINDOW_NUM;
}

/* receiver finalization, called once at the very end.
   you may find that you don't need it, in which case you can leave it blank.
   in certain cases, you might want to use this opportunity to release some
   memory you allocated in Receiver_init(). */
void Receiver_Final()
{
    fprintf(stdout, "At %.2fs: receiver finalizing ...\n", GetSimulationTime());
    Clean_Window();
}

/* event handler, called when a packet is passed from the lower layer at the
   receiver */
void Receiver_FromLowerLayer(struct packet *pkt)
{
    /* 1-byte header indicating the size of the payload */
    // int header_size = 1;

    // /* construct a message and deliver to the upper layer */
    // struct message *msg = (struct message *)malloc(sizeof(struct message));
    // ASSERT(msg != NULL);

    // msg->size = pkt->data[0];

    // /* sanity check in case the packet is corrupted */
    // if (msg->size < 0)
    //     msg->size = 0;
    // if (msg->size > RDT_PKTSIZE - header_size)
    //     msg->size = RDT_PKTSIZE - header_size;

    // msg->data = (char *)malloc(msg->size);
    // ASSERT(msg->data != NULL);
    // memcpy(msg->data, pkt->data + header_size, msg->size);
    // Receiver_ToUpperLayer(msg);

    // /* don't forget to free the space */
    // if (msg->data != NULL)
    //     free(msg->data);
    // if (msg != NULL)
    //     free(msg);

    // printf("receive:%d\n",pkt->data[3]);

    if (!Receiver_Check_Checksum(pkt))
        return;

    Change_Window(pkt);
    pkt->data[4] = pkt->data[3];
    pkt->data[0] = Receiver_Make_Checksum(pkt);

    Receiver_ToLowerLayer(pkt);
}
