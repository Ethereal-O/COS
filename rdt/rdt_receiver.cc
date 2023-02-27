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

#define HEADER_SIZE 6
#define WINDOW_SIZE 10
#define MAX_WINDOW_NUM (10 * WINDOW_SIZE)

// define the window
struct window
{
    int begin_num = -WINDOW_SIZE;
    packet *pkts[WINDOW_SIZE];
};

std::unique_ptr<window> receiver_pkt_window;
unsigned short receiver_CrcTable[256];

void Receiver_Make_Checksum_Table()
{
    for (int i = 0; i < 256; i++)
    {
        unsigned short Crc = i;
        for (int j = 0; j < 8; j++)
            if (Crc & 0x1)
                Crc = (Crc >> 1) ^ 0xA001;
            else
                Crc >>= 1;
        receiver_CrcTable[i] = Crc;
    }
}

short Receiver_Make_Checksum(packet *pkt)
{
    // first init to zero
    short checksum = 0x0000;

    // add all the characters in payload, but jump the checksum bits
    for (int i = 2; i < RDT_PKTSIZE; i++)
        checksum = (checksum >> 8) ^ receiver_CrcTable[(checksum & 0xFF) ^ pkt->data[i]];

    return checksum;
}

bool Receiver_Check_Checksum(packet *pkt)
{
    // first init to zero
    short checksum = 0x0000;

    // add all the characters in payload, but jump the checksum bits
    for (int i = 2; i < RDT_PKTSIZE; i++)
        checksum = (checksum >> 8) ^ receiver_CrcTable[(checksum & 0xFF) ^ pkt->data[i]];

    return (short)checksum == *(short *)(pkt->data);
}

void Clean_Window(packet *pkt)
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

        msg->size = pkt->data[2];

        // sanity check in case the packet is corrupted
        if (msg->size < 0)
            msg->size = 0;
        if (msg->size > RDT_PKTSIZE - HEADER_SIZE)
            msg->size = RDT_PKTSIZE - HEADER_SIZE;

        msg->data = (char *)malloc(msg->size);
        ASSERT(msg->data != NULL);
        memcpy(msg->data, pkt->data + HEADER_SIZE, msg->size);
        Receiver_ToUpperLayer(msg);
    }

    if (pkt == NULL)
        return;

    // clean it
    for (i = 0; i < WINDOW_SIZE; i++)
    {
        receiver_pkt_window->pkts[i] = NULL;
    }
    receiver_pkt_window->begin_num = pkt->data[4] - pkt->data[4] % WINDOW_SIZE;
}

void Change_Window(packet *pkt)
{
    int seq = pkt->data[4];
    if (seq >= receiver_pkt_window->begin_num && seq < receiver_pkt_window->begin_num + WINDOW_SIZE)
    {
        // now window
        receiver_pkt_window->pkts[seq % WINDOW_SIZE] = new packet();
        memcpy(receiver_pkt_window->pkts[seq % WINDOW_SIZE]->data, pkt->data, RDT_PKTSIZE);
    }
    else if (seq >= (receiver_pkt_window->begin_num + WINDOW_SIZE) % MAX_WINDOW_NUM)
    {
        // new window
        Clean_Window(pkt);
        receiver_pkt_window->pkts[seq % WINDOW_SIZE] = new packet();
        memcpy(receiver_pkt_window->pkts[seq % WINDOW_SIZE]->data, pkt->data, RDT_PKTSIZE);
    }
    else if (seq < receiver_pkt_window->begin_num)
    {
        // drop it
        // printf("error drop");
    }
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

    // make checksum table
    Receiver_Make_Checksum_Table();
}

/* receiver finalization, called once at the very end.
   you may find that you don't need it, in which case you can leave it blank.
   in certain cases, you might want to use this opportunity to release some
   memory you allocated in Receiver_init(). */
void Receiver_Final()
{
    fprintf(stdout, "At %.2fs: receiver finalizing ...\n", GetSimulationTime());

    // clean window
    Clean_Window(NULL);
}

/* event handler, called when a packet is passed from the lower layer at the
   receiver */
void Receiver_FromLowerLayer(struct packet *pkt)
{
    // check checksum
    if (!Receiver_Check_Checksum(pkt))
        return;

    // change window
    Change_Window(pkt);

    // return to sender
    pkt->data[5] = pkt->data[4];
    *(short *)pkt->data = Receiver_Make_Checksum(pkt);
    Receiver_ToLowerLayer(pkt);
}
