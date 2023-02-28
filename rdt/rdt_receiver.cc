/*
 * FILE: rdt_receiver.cc
 * DESCRIPTION: Reliable data transfer receiver.
 * NOTE:
 *       In this implementation, the packet format is laid out as
 *       the following:
 *
 *       |<-  4 bytes ->|<-  1 byte  ->|<-  1 byte  ->|<-  1 byte  ->|<-  1 byte  ->|<-             the rest            ->|
 *       |   checksum   | payload_size |     index    |    seq_num   |  ack_seq_num |<-             payload             ->|
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>

#include "rdt_struct.h"
#include "rdt_receiver.h"

#define HEADER_SIZE 9
#define WINDOW_SIZE 10
#define MAX_WINDOW_NUM (10 * WINDOW_SIZE)
#define CRC_KEY 0xEDB88320
#define OVER_CIRCLE (4 * WINDOW_SIZE)

struct header
{
    // checksum 4 bytes
    int checksum;
    // // checksum 2 bytes
    // short checksum;
    int pkt_ID;
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

void Receiver_Make_Checksum_Table()
{
    for (int i = 0; i < 256; i++)
    {
        decltype(receiver_header.checksum) Crc = i;
        for (int j = 0; j < 8; j++)
            if (Crc & 0x1)
                Crc = (Crc >> 1) ^ CRC_KEY;
            else
                Crc >>= 1;
        receiver_CrcTable[i] = Crc;
    }
}

decltype(receiver_header.checksum) Receiver_Make_Checksum(packet *pkt)
{
    // first init to zero
    decltype(receiver_header.checksum) checksum = 0x0;

    // add all the characters in payload, but jump the checksum bits
    for (int i = sizeof(receiver_header.checksum); i < sizeof(receiver_header.checksum) + sizeof(int); i++)
    checksum += i*pkt->data[i];
        // checksum = (checksum >> 8) ^ receiver_CrcTable[(checksum & 0xFF) ^ pkt->data[i]];

    return checksum;
}

bool Receiver_Check_Checksum(packet *pkt)
{
    // first init to zero
    decltype(receiver_header.checksum) checksum = 0x0;

    // add all the characters in payload, but jump the checksum bits
    for (int i = sizeof(receiver_header.checksum); i < RDT_PKTSIZE; i++)
    {
        checksum += i*pkt->data[i];
        // checksum = (checksum >> 8) ^ receiver_CrcTable[(checksum & 0xFF) ^ pkt->data[i]];
        // printf("rec: %d %d %d\n",i,checksum,pkt->data[i]);
    }
    // exit(0);
        // checksum += i*pkt->data[i];
        // checksum = (checksum >> 8) ^ receiver_CrcTable[(checksum & 0xFF) ^ pkt->data[i]];

    // printf("%d %d\n", (decltype(receiver_header.checksum))checksum, *(decltype(receiver_header.checksum) *)(pkt->data));

    return (decltype(receiver_header.checksum))checksum == *(decltype(receiver_header.checksum) *)(pkt->data);
}

void Reply(int ack)
{
    packet pkt;
    memcpy(pkt.data + sizeof(receiver_header.checksum), &ack, sizeof(int));
    decltype(receiver_header.checksum) checksum = Receiver_Make_Checksum(&pkt);
    memcpy(pkt.data, &checksum, sizeof(decltype(receiver_header.checksum)));
    Receiver_ToLowerLayer(&pkt);
}

/* receiver initialization, called once at the very beginning */
void Receiver_Init()
{
    fprintf(stdout, "At %.2fs: receiver initializing ...\n", GetSimulationTime());

    // init pkt buffer
    receiver_pkt_window = std::make_unique<window>();
    for (int i = 0; i < WINDOW_SIZE; i++)
        receiver_pkt_window->pkts[i] = NULL;

    // make checksum table
    Receiver_Make_Checksum_Table();

    printf("rec:");
    for (int i=0;i<10;i++)
    {
        printf("%d ",receiver_CrcTable[i]);
    }
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
    // // check checksum
    if (!Receiver_Check_Checksum(pkt))
        return;

    // printf("%d %d %d\n",*(int *)(pkt->data),*(int *)(pkt->data+4),receiver_pkt_window->ack_num);

    int pktID = 0, payloadSize = 0;
    pktID = *(decltype(receiver_header.pkt_ID) *)(pkt->data + sizeof(receiver_header.checksum));
    // 如果收到了window内的pkt，则将其置位valid并存入window，并且返回目前的ack
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
    // 如果是以前的pkt，返回目前的ack
    else if (pktID != receiver_pkt_window->ack_num)
    {
        Reply(receiver_pkt_window->ack_num - 1);
        return;
    }
    // 否则说明是目前window的下界，更新window
    while (true)
    {
        receiver_pkt_window->ack_num++;
        payloadSize = pkt->data[HEADER_SIZE - 1];

        struct message *msg = (struct message *)malloc(sizeof(struct message));
        ASSERT(msg != NULL);

        msg->size = pkt->data[sizeof(receiver_header.checksum) + sizeof(receiver_header.pkt_ID)];

        // sanity check in case the packet is corrupted
        if (msg->size < 0)
            msg->size = 0;
        if (msg->size > RDT_PKTSIZE - HEADER_SIZE)
            msg->size = RDT_PKTSIZE - HEADER_SIZE;

        msg->data = (char *)malloc(msg->size);
        ASSERT(msg->data != NULL);
        memcpy(msg->data, pkt->data + HEADER_SIZE, msg->size);
        Receiver_ToUpperLayer(msg);

        // 从window中检查是否有包,有的话直接取出即可,并consume后置位invalid
        if (receiver_pkt_window->valid[receiver_pkt_window->ack_num % WINDOW_SIZE])
        {
            pkt = receiver_pkt_window->pkts[receiver_pkt_window->ack_num % WINDOW_SIZE];
            memcpy(&pktID, pkt->data + sizeof(short), sizeof(int));
            receiver_pkt_window->valid[receiver_pkt_window->ack_num % WINDOW_SIZE] = false;
        }
        else
            break;
    };
    // 更新window之后，上界作为新的ack
    Reply(pktID);
}
