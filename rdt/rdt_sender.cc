/*
 * FILE: rdt_sender.cc
 * DESCRIPTION: Reliable data transfer sender.
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
#include <list>
#include <memory>

#include "rdt_struct.h"
#include "rdt_sender.h"

#define HEADER_SIZE 10
#define WINDOW_SIZE 10
#define TIME_OUT 0.3
// #define TIME_OUT 0.1

struct header
{
    // checksum 4 bytes
    int checksum;
    // // checksum 2 bytes
    // short checksum;
    int pkt_ID;
    char has_more;
    char payload_size;
} sender_header;

struct window
{
    // pkt ID
    int pkt_ID = 0;
    // pkt need to be sended future
    int pkt_num = 0;
    // pkt need to be sended now
    int pkt_send_ID = 0;
    // pkt that has been ACKed
    int ack_pkt_num = 0;
    // pkts
    packet *pkts[WINDOW_SIZE];
};

std::unique_ptr<std::list<packet *>> sender_pkt_list;
std::unique_ptr<window> sender_pkt_window;
decltype(sender_header.checksum) sender_CrcTable[256];

void Print_List()
{
    // for debug, to print the info of pkt list
    for (auto pkt : *sender_pkt_list)
        printf("%d %d %d\n", pkt->data[1], pkt->data[2], pkt->data[3]);
}

decltype(sender_header.checksum) Sender_Make_Checksum(packet *pkt)
{
    // first init to zero
    decltype(sender_header.checksum) checksum = 0x0;

    // add all the characters in payload, but jump the checksum bits
    for (int i = sizeof(sender_header.checksum); i < RDT_PKTSIZE; i++)
        checksum += i * pkt->data[i];

    return checksum;
}

bool Sender_Check_Checksum(packet *pkt)
{
    // first init to zero
    decltype(sender_header.checksum) checksum = 0x0;

    // add all the characters in payload, but jump the checksum bits
    for (int i = sizeof(sender_header.checksum); i < (int)(sizeof(sender_header.checksum) + sizeof(int)); i++)
        checksum += i * pkt->data[i];

    return (decltype(sender_header.checksum))checksum == *(decltype(sender_header.checksum) *)pkt->data;
}

void Add_Message(message *msg)
{
    // maximum payload size
    int maxpayload_size = RDT_PKTSIZE - HEADER_SIZE;

    // split the message if it is too big
    // the cursor always points to the first unsent byte in the message
    int index = 0;
    while (msg->size - index * maxpayload_size > maxpayload_size)
    {
        // fill in the packet
        packet *pkt = new packet();
        pkt->data[sizeof(sender_header.checksum) + sizeof(sender_header.pkt_ID)] = true;
        pkt->data[sizeof(sender_header.checksum) + sizeof(sender_header.pkt_ID) + sizeof(sender_header.has_more)] = maxpayload_size;
        memcpy(pkt->data + HEADER_SIZE, msg->data + index * maxpayload_size, maxpayload_size);

        // add it to the list
        sender_pkt_list->emplace_back(pkt);

        // move the cursor
        index++;
    }

    // prepare the last packet
    if (msg->size > index * maxpayload_size)
    {
        // fill in the packet
        packet *pkt = new packet();
        pkt->data[sizeof(sender_header.checksum) + sizeof(sender_header.pkt_ID)] = false;
        pkt->data[sizeof(sender_header.checksum) + sizeof(sender_header.pkt_ID) + sizeof(sender_header.has_more)] = msg->size - index * maxpayload_size;
        memcpy(pkt->data + HEADER_SIZE, msg->data + index * maxpayload_size, pkt->data[sizeof(sender_header.checksum) + sizeof(sender_header.pkt_ID) + sizeof(sender_header.has_more)]);

        // add it to the list
        sender_pkt_list->emplace_back(pkt);
    }
}

void Send()
{
    // send packets
    packet *pkt;
    while (sender_pkt_window->pkt_send_ID < sender_pkt_window->pkt_ID)
    {
        pkt = sender_pkt_window->pkts[sender_pkt_window->pkt_send_ID % WINDOW_SIZE];
        Sender_ToLowerLayer(pkt);
        sender_pkt_window->pkt_send_ID++;
    }
}

void Update_Window()
{
    while (sender_pkt_window->pkt_num < WINDOW_SIZE && sender_pkt_list->size() > 0)
    {
        packet *pkt = sender_pkt_list->front();
        sender_pkt_list->pop_front();

        // set id and checksum
        *(decltype(sender_header.pkt_ID) *)(pkt->data + sizeof(sender_header.checksum)) = sender_pkt_window->pkt_ID;
        *(decltype(sender_header.checksum) *)pkt->data = Sender_Make_Checksum(pkt);

        // fill window with packet
        sender_pkt_window->pkts[sender_pkt_window->pkt_ID % WINDOW_SIZE] = pkt;
        sender_pkt_window->pkt_ID++;
        sender_pkt_window->pkt_num++;
    }
    Send();
}

/* sender initialization, called once at the very beginning */
void Sender_Init()
{
    fprintf(stdout, "At %.2fs: sender initializing ...\n", GetSimulationTime());

    // init pkt buffer
    sender_pkt_list = std::make_unique<std::list<packet *>>();

    // init pkt window
    sender_pkt_window = std::make_unique<window>();
    for (int i = 0; i < WINDOW_SIZE; i++)
        sender_pkt_window->pkts[i] = NULL;
}

/* sender finalization, called once at the very end.
   you may find that you don't need it, in which case you can leave it blank.
   in certain cases, you might want to take this opportunity to release some
   memory you allocated in Sender_init(). */
void Sender_Final()
{
    fprintf(stdout, "At %.2fs: sender finalizing ...\n", GetSimulationTime());
}

/* event handler, called when a message is passed from the upper layer at the
   sender */
void Sender_FromUpperLayer(struct message *msg)
{
    Add_Message(msg);

    if (Sender_isTimerSet())
        return;

    // it is only called the first time
    Sender_StartTimer(TIME_OUT);
    Update_Window();
}

/* event handler, called when a packet is passed from the lower layer at the
   sender */
void Sender_FromLowerLayer(struct packet *pkt)
{
    // check it's checksum
    if (!Sender_Check_Checksum(pkt))
        return;

    int ack = *(int *)(pkt->data + sizeof(sender_header.checksum));
    if (sender_pkt_window->ack_pkt_num <= ack && ack < sender_pkt_window->pkt_ID)
    {
        Sender_StartTimer(TIME_OUT);

        // update pkt num
        sender_pkt_window->pkt_num -= (ack - sender_pkt_window->ack_pkt_num + 1);

        // update up bound
        sender_pkt_window->ack_pkt_num = ack + 1;
        Update_Window();
    }

    // no packet now
    if (ack == sender_pkt_window->pkt_ID - 1)
        Sender_StopTimer();
}

/* event handler, called when the timer expires */
void Sender_Timeout()
{
    Sender_StartTimer(TIME_OUT);
    sender_pkt_window->pkt_send_ID = sender_pkt_window->ack_pkt_num;
    Update_Window();
}
