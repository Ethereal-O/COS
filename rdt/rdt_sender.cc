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

#define HEADER_SIZE 5
#define WINDOW_SIZE 10
#define TIME_OUT 500
#define MAX_WINDOW_NUM (10 * WINDOW_SIZE)

struct header
{
    int checksum;
    int payload_size;
    int index;
    int sequence_number;
    int acknowledgment_sequence_number;
};

struct window
{
    int begin_num = -WINDOW_SIZE;
    bool is_ack[WINDOW_SIZE];
    packet *pkts[WINDOW_SIZE];
};

std::unique_ptr<std::list<packet *>> pkt_list;
std::unique_ptr<window> pkt_window;

void Print_List()
{
    // for debug, to print the info of pkt list
    for (auto pkt : *pkt_list)
        printf("%d %d %d\n", pkt->data[1], pkt->data[2], pkt->data[3]);
}

int Sender_Make_Checksum(packet *pkt)
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

bool Sender_Check_Checksum(packet *pkt)
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
        pkt->data[1] = maxpayload_size;
        pkt->data[2] = index;
        memcpy(pkt->data + HEADER_SIZE, msg->data + index * maxpayload_size, maxpayload_size);

        // make checksum
        pkt->data[0] = Sender_Make_Checksum(pkt);

        // add it to the list
        pkt_list->emplace_back(pkt);

        // move the cursor
        index++;
    }

    // prepare the last packet
    if (msg->size > index * maxpayload_size)
    {
        // fill in the packet
        packet *pkt = new packet();
        pkt->data[1] = msg->size - index * maxpayload_size;
        pkt->data[2] = index;
        memcpy(pkt->data + HEADER_SIZE, msg->data + index * maxpayload_size, pkt->data[1]);

        // make checksum
        pkt->data[0] = Sender_Make_Checksum(pkt);

        // add it to the list
        pkt_list->emplace_back(pkt);
    }
}

void Resend()
{
    // send packets
    for (int i = 0; i < WINDOW_SIZE; i++)
    {
        if (pkt_window->pkts[i] == NULL || pkt_window->is_ack[i] == true)
            continue;
        Sender_ToLowerLayer(pkt_window->pkts[i]);
    }

    // set timer
    Sender_StartTimer(TIME_OUT);
}

void Update_Window()
{
    // check if all pkts are acked
    bool is_all_ack = true;
    for (int i = 0; i < WINDOW_SIZE; i++)
        is_all_ack = is_all_ack && pkt_window->is_ack[i];

    if (!is_all_ack)
        return;

    // stop timer
    if (Sender_isTimerSet())
        Sender_StopTimer();

    for (int i = 0; i < WINDOW_SIZE; i++)
    {
        if (pkt_window->pkts[i] == NULL)
            continue;
        // delete pkt_window->pkts[i];
        pkt_window->pkts[i] = NULL;
        pkt_window->is_ack[i] = true;
    }

    // init the window
    pkt_window->begin_num = (unsigned)(char)((pkt_window->begin_num + WINDOW_SIZE) % MAX_WINDOW_NUM);
    // if (pkt_window->begin_num<0)
    // pkt_window->begin_num = 0;
    // else
    // pkt_window->begin_num = (unsigned)(char)((pkt_window->begin_num + max(WINDOW_SIZE,pkt_list->size())) % MAX_WINDOW_NUM);
    int i;
    for (i = 0; i < WINDOW_SIZE; i++)
    {
        packet *top_pkt = pkt_list->front();
        if (top_pkt == NULL)
            break;
        pkt_window->pkts[i] = top_pkt;
        pkt_window->pkts[i]->data[3] = pkt_window->begin_num + i;
        pkt_window->pkts[i]->data[0] = Sender_Make_Checksum(pkt_window->pkts[i]);
        pkt_window->is_ack[i] = false;
        pkt_list->pop_front();
    }

    // no other packets
    if (i == 0)
        return;

    Resend();
}

/* sender initialization, called once at the very beginning */
void Sender_Init()
{
    fprintf(stdout, "At %.2fs: sender initializing ...\n", GetSimulationTime());
    // init pkt buffer
    pkt_list = std::make_unique<std::list<packet *>>();

    // init pkt window
    pkt_window = std::make_unique<window>();
    for (int i = 0; i < WINDOW_SIZE; i++)
    {
        pkt_window->is_ack[i] = true;
        pkt_window->pkts[i] = NULL;
    }
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
    // it is only called the first time
    Update_Window();
}

/* event handler, called when a packet is passed from the lower layer at the
   sender */
void Sender_FromLowerLayer(struct packet *pkt)
{
    if (!Sender_Check_Checksum(pkt) || (pkt->data[4] < (char)pkt_window->begin_num))
        return;

    // update window
    pkt_window->is_ack[pkt->data[4] % WINDOW_SIZE] = true;
    Update_Window();
}

/* event handler, called when the timer expires */
void Sender_Timeout()
{
    Resend();
}
