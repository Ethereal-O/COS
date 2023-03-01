/*
 * FILE: rdt_receiver.cc
 * DESCRIPTION: Reliable data transfer receiver.
 * NOTE:
 *       In this implementation, the packet format is laid out as
 *       the following:
 *
 *       |<-  4 bytes ->|<-  4 byte  ->|<-             the rest            ->|
 *       |   checksum   |    pkt_ID    |<-             payload             ->|
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>

#include "rdt_struct.h"
#include "rdt_receiver.h"

#define HEADER_SIZE 10
#define WINDOW_SIZE 10
#define MAX_WINDOW_NUM (10 * WINDOW_SIZE)

struct header
{
    // checksum 4 bytes
    int checksum;
    // // checksum 2 bytes
    // short checksum;
    int pkt_ID;
    char has_more;
    char payload_size;
} receiver_header;

// define the window
struct window
{
    int ack_num = 0;
    bool valid[WINDOW_SIZE];
    packet *pkts[WINDOW_SIZE];
};

std::unique_ptr<window> receiver_pkt_window;
decltype(receiver_header.checksum) receiver_CrcTable[256];

decltype(receiver_header.checksum) Receiver_Make_Checksum(packet *pkt)
{
    // first init to zero
    decltype(receiver_header.checksum) checksum = 0x0;

    // add all the characters in payload, but jump the checksum bits
    for (int i = sizeof(receiver_header.checksum); i < (int)(sizeof(receiver_header.checksum) + sizeof(int)); i++)
        checksum += i * pkt->data[i];

    return checksum;
}

bool Receiver_Check_Checksum(packet *pkt)
{
    // first init to zero
    decltype(receiver_header.checksum) checksum = 0x0;

    // add all the characters in payload, but jump the checksum bits
    for (int i = sizeof(receiver_header.checksum); i < RDT_PKTSIZE; i++)
        checksum += i * pkt->data[i];

    return (decltype(receiver_header.checksum))checksum == *(decltype(receiver_header.checksum) *)(pkt->data);
}

void Reply(int ack)
{
    packet *pkt = new packet();
    *(int *)(pkt->data + sizeof(receiver_header.checksum)) = ack;
    *(decltype(receiver_header.checksum) *)pkt->data = Receiver_Make_Checksum(pkt);
    Receiver_ToLowerLayer(pkt);
}

void Wrapper_Receiver_ToUpperLayer(packet *pkt)
{
    struct message *msg = (struct message *)malloc(sizeof(struct message));
    ASSERT(msg != NULL);

    msg->size = pkt->data[sizeof(receiver_header.checksum) + sizeof(receiver_header.pkt_ID) + sizeof(receiver_header.has_more)];

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

void Slide_Window(packet *pkt)
{
    int pktID = *(decltype(receiver_header.pkt_ID) *)(pkt->data + sizeof(receiver_header.checksum));

    // now window
    if (pktID > receiver_pkt_window->ack_num && pktID < receiver_pkt_window->ack_num + WINDOW_SIZE)
    {
        if (!receiver_pkt_window->valid[pktID % WINDOW_SIZE])
        {
            receiver_pkt_window->pkts[pktID % WINDOW_SIZE] = new packet();
            memcpy(receiver_pkt_window->pkts[pktID % WINDOW_SIZE]->data, pkt->data, RDT_PKTSIZE);
            receiver_pkt_window->valid[pktID % WINDOW_SIZE] = true;
        }
        Reply(receiver_pkt_window->ack_num - 1);
        return;
    }

    // old window
    else if (pktID != receiver_pkt_window->ack_num)
    {
        Reply(receiver_pkt_window->ack_num - 1);
        return;
    }

    // new window
    while (true)
    {
        receiver_pkt_window->ack_num++;

        Wrapper_Receiver_ToUpperLayer(pkt);

        // check duplicate
        if (receiver_pkt_window->valid[receiver_pkt_window->ack_num % WINDOW_SIZE])
        {
            pkt = receiver_pkt_window->pkts[receiver_pkt_window->ack_num % WINDOW_SIZE];
            pktID = *(int *)(pkt->data + sizeof(receiver_header.checksum));
            receiver_pkt_window->valid[receiver_pkt_window->ack_num % WINDOW_SIZE] = false;
        }
        else
        {
            Reply(pktID);
            return;
        }
    };
}

/* receiver initialization, called once at the very beginning */
void Receiver_Init()
{
    fprintf(stdout, "At %.2fs: receiver initializing ...\n", GetSimulationTime());

    // init pkt buffer
    receiver_pkt_window = std::make_unique<window>();
    for (int i = 0; i < WINDOW_SIZE; i++)
        receiver_pkt_window->pkts[i] = NULL;
}

/* receiver finalization, called once at the very end.
   you may find that you don't need it, in which case you can leave it blank.
   in certain cases, you might want to use this opportunity to release some
   memory you allocated in Receiver_init(). */
void Receiver_Final()
{
    fprintf(stdout, "At %.2fs: receiver finalizing ...\n", GetSimulationTime());
}

/* event handler, called when a packet is passed from the lower layer at the
   receiver */
void Receiver_FromLowerLayer(struct packet *pkt)
{
    // check checksum
    if (!Receiver_Check_Checksum(pkt))
        return;

    Slide_Window(pkt);
}
