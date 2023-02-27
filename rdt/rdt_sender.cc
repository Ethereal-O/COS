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

#define HEADER_SIZE 8
#define WINDOW_SIZE 10
#define TIME_OUT 0.4
#define MAX_WINDOW_NUM (10 * WINDOW_SIZE)
#define CRC_KEY 0xEDB88320

struct header
{
    // checksum 4 bytes
    int checksum;
    // // checksum 2 bytes
    // short checksum;
    char payload_size;
    char index;
    char sequence_number;
    char acknowledgment_sequence_number;
} sender_header;

struct window
{
    int begin_num = -WINDOW_SIZE;
    bool is_ack[WINDOW_SIZE];
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

void Sender_Make_Checksum_Table()
{
    for (int i = 0; i < 256; i++)
    {
        decltype(sender_header.checksum) Crc = i;
        for (int j = 0; j < 8; j++)
            if (Crc & 0x1)
                Crc = (Crc >> 1) ^ CRC_KEY;
            else
                Crc >>= 1;
        sender_CrcTable[i] = Crc;
    }
}

decltype(sender_header.checksum) Sender_Make_Checksum(packet *pkt)
{
    // first init to zero
    decltype(sender_header.checksum) checksum = 0x0;

    // add all the characters in payload, but jump the checksum bits
    for (int i = sizeof(sender_header.checksum); i < RDT_PKTSIZE; i++)
        checksum = (checksum >> 8) ^ sender_CrcTable[(checksum & 0xFF) ^ pkt->data[i]];

    return checksum;
}

bool Sender_Check_Checksum(packet *pkt)
{
    // first init to zero
    decltype(sender_header.checksum) checksum = 0x0;

    // add all the characters in payload, but jump the checksum bits
    for (int i = sizeof(sender_header.checksum); i < RDT_PKTSIZE; i++)
        checksum = (checksum >> 8) ^ sender_CrcTable[(checksum & 0xFF) ^ pkt->data[i]];

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
        pkt->data[sizeof(sender_header.checksum)] = maxpayload_size;
        pkt->data[sizeof(sender_header.checksum) + 1] = index;
        memcpy(pkt->data + HEADER_SIZE, msg->data + index * maxpayload_size, maxpayload_size);

        // make checksum
        *(decltype(sender_header.checksum) *)pkt->data = Sender_Make_Checksum(pkt);

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
        pkt->data[sizeof(sender_header.checksum)] = msg->size - index * maxpayload_size;
        pkt->data[sizeof(sender_header.checksum) + 1] = index;
        memcpy(pkt->data + HEADER_SIZE, msg->data + index * maxpayload_size, pkt->data[sizeof(sender_header.checksum)]);

        // make checksum
        *(decltype(sender_header.checksum) *)pkt->data = Sender_Make_Checksum(pkt);

        // add it to the list
        sender_pkt_list->emplace_back(pkt);
    }
}

void Resend()
{
    // send packets
    for (int i = 0; i < WINDOW_SIZE; i++)
    {
        if (sender_pkt_window->pkts[i] == NULL || sender_pkt_window->is_ack[i] == true)
            continue;
        Sender_ToLowerLayer(sender_pkt_window->pkts[i]);
    }

    // set timer
    Sender_StartTimer(TIME_OUT);
}

void Update_Window()
{
    // check if all pkts are acked
    bool is_all_ack = true;
    for (int i = 0; i < WINDOW_SIZE; i++)
        is_all_ack = is_all_ack && sender_pkt_window->is_ack[i];

    if (!is_all_ack)
        return;

    // stop timer
    if (Sender_isTimerSet())
        Sender_StopTimer();

    for (int i = 0; i < WINDOW_SIZE; i++)
    {
        if (sender_pkt_window->pkts[i] == NULL)
            continue;
        delete sender_pkt_window->pkts[i];
        sender_pkt_window->pkts[i] = NULL;
        sender_pkt_window->is_ack[i] = true;
    }

    // init the window
    sender_pkt_window->begin_num = (unsigned)(char)((sender_pkt_window->begin_num + WINDOW_SIZE) % MAX_WINDOW_NUM);

    // move pkts to window
    int i;
    for (i = 0; i < WINDOW_SIZE; i++)
    {
        packet *top_pkt = sender_pkt_list->front();
        if (top_pkt == NULL)
            break;
        sender_pkt_window->pkts[i] = top_pkt;
        sender_pkt_window->pkts[i]->data[sizeof(sender_header.checksum) + 2] = sender_pkt_window->begin_num + i;
        *(decltype(sender_header.checksum) *)sender_pkt_window->pkts[i]->data = Sender_Make_Checksum(sender_pkt_window->pkts[i]);
        sender_pkt_window->is_ack[i] = false;
        sender_pkt_list->pop_front();
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
    sender_pkt_list = std::make_unique<std::list<packet *>>();

    // init pkt window
    sender_pkt_window = std::make_unique<window>();
    for (int i = 0; i < WINDOW_SIZE; i++)
    {
        sender_pkt_window->is_ack[i] = true;
        sender_pkt_window->pkts[i] = NULL;
    }

    // make checksum table
    Sender_Make_Checksum_Table();
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
    if (!Sender_Check_Checksum(pkt) || (pkt->data[sizeof(sender_header.checksum) + 3] < (char)sender_pkt_window->begin_num))
        return;

    // update window
    sender_pkt_window->is_ack[pkt->data[sizeof(sender_header.checksum) + 3] % WINDOW_SIZE] = true;
    Update_Window();
}

/* event handler, called when the timer expires */
void Sender_Timeout()
{
    Resend();
}
