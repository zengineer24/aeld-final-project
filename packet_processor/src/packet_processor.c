//#include <pcap/pcap.h>
#include <stdint.h>
#include <pthread.h>
#include <syslog.h>
#include <linux/nl80211.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
#include <net/if.h>
#include <math.h>
#include <unistd.h>
#include "signal.h"
#include "circular-buffer.h"

#define PCAP_ERRBUF_SIZE 1024

int keepRunning = 1;

typedef struct params
{
    //const char* source;
    struct aesd_circular_buffer* dataBufPtr;
    struct aesd_buffer_entry* newEntry;

}threadParams;

//forward declaration of threads for use in main
void* packetReceiverThread(void* arg);
void* packetProcessorThread(void* arg);
static int packetCallback(struct nl_msg *msg, void *arg);

void terminateHandler(int signum)
{
    keepRunning = 0;
}

/**
* This is the main function which calls the initialization, functional, and termination code.
*/
int main(int argc, char const *argv[])
{
    //declare variables
    struct sigaction terminateAction;
    pid_t daemonPid;
    struct sigevent sev;
    bool success = false;
    threadParams* td = malloc(sizeof(threadParams));
    
    //setup signals and make sure register is successful
    memset(&terminateAction, 0, sizeof(terminateAction));
    terminateAction.sa_handler = terminateHandler;

    //Setup sig handlers
    if(sigaction(SIGTERM, &terminateAction, NULL) != 0)
    {
        success = false;
        return success;
    }
    if(sigaction(SIGINT, &terminateAction, NULL) != 0)
    {
        success = false;
        return success;
    }

    //if -d flag asserted, start daemon
    if((argc == 2) && (strcmp(argv[1], "-d") == 0))
    {
        daemonPid = fork();
        if(daemonPid == -1)
        {
            return -1;
        } 
        else if(daemonPid != 0)
        {
            exit(EXIT_SUCCESS);
        }

        if(setsid() == -1)
        {
            return -1;
        }

        if(chdir("/") == -1)
        {
            return -1;
        }
    }

    //open user log to use with syslog for error reporting
    openlog(NULL, 0, LOG_USER);

    //Setup buffer, since this should be shared among threads
    struct aesd_circular_buffer* packetBuffer = malloc(sizeof(struct aesd_circular_buffer));
    if(packetBuffer == NULL)
    {

    }

    struct aesd_buffer_entry* newEntry = malloc(sizeof(struct aesd_buffer_entry));
    if(newEntry == NULL)
    {

    }
    memset(newEntry, 0, sizeof(struct aesd_buffer_entry));

    aesd_circular_buffer_init(packetBuffer);

    //init threadParams
    td->dataBufPtr = packetBuffer;
    td->newEntry = newEntry;
    //td->source = NULL;

    //Setup threads
    pthread_t receiver, processor;
    pthread_create(&receiver, NULL, packetReceiverThread, td);
    pthread_create(&processor, NULL, packetProcessorThread, td);


    //cleanup when program terminated
    pthread_join(receiver, NULL);
    pthread_join(processor, NULL);

    syslog(LOG_DEBUG, "Caught signal, exiting");

    closelog();

    free(packetBuffer);
    free(td);
    return 0;

}

void* packetReceiverThread(void* arg)
{
    printf("Starting receiver thread\n");
    //Initial setup only needs to be done once
    struct nl_sock* packetSock = nl_socket_alloc();
    if(packetSock == NULL)
    {
        return;
    }
    if(genl_connect(packetSock) == 0)
    {
        nl_socket_free(packetSock);
        return;
    }

    int sockId = genl_ctrl_resolve(packetSock, "nl80211");
    if (sockId < 0) {
        syslog(LOG_ERR, "nl80211 not found.\n");
        nl_socket_free(packetSock);
        return;
    }
    
    struct nl_msg* msg = nlmsg_alloc();
    if(msg == NULL)
    {
        syslog(LOG_ERR, "Failed to allocate msg");
        nl_socket_free(packetSock);
        return;
    }

    struct nl_cb* cb = nl_cb_alloc(NL_CB_CUSTOM);
    if(cb == NULL)
    {
        syslog(LOG_ERR, "Unable to allocated call-back");
        nl_socket_free(packetSock);
        return;
    }

    if(nl_cb_set(cb, NL_CB_MSG_IN, NL_CB_CUSTOM, packetCallback, arg) != 0)
    {
        syslog(LOG_ERR, "Unable to set callback function");
        nl_socket_free(packetSock);
        return;
    }

    while(keepRunning)
    {
        //Keep creating new messages (in same alloc'd message) and send; wait for response
        void* usrHdr = genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, sockId, 0, 0, NL80211_CMD_GET_STATION, 0);
        if(usrHdr == NULL)
        {
            syslog(LOG_ERR, "Unable to populate message");
            nlmsg_free(msg);
            nl_socket_free(packetSock);
            return;
        }

        int index = if_nametoindex("enp0s3");
        NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, index);

        if(nl_send_auto(packetSock, msg) < 0)
        {
            syslog(LOG_ERR, "Message send failed");
            nlmsg_free(msg);
            nl_socket_free(packetSock);
            return;
        }

        if(nl_recvmsgs_default(packetSock) != 0)
        {
            syslog(LOG_ERR, "Message receive failed");
            nlmsg_free(msg);
            nl_socket_free(packetSock);
            return;
        }

        //sleep 150 ms between reads
        usleep(150000);
    }

    //If we get terminate signal and exit loop
    nlmsg_free(msg);
    nl_socket_free(packetSock);
    //Good return
    return;

    //Needed in case nla fails
    nla_put_failure:
        syslog(LOG_ERR, "Unable to build message!");
        return;
}

void* packetProcessorThread(void* arg)
{
    printf("Starting processor thread\n");
    threadParams* params = ((threadParams*)arg);

    while(keepRunning)
    {
        //Wait for full buffer so sample size is sufficient
        if(params->dataBufPtr->full)
        {
            uint8_t index;
            int8_t localBuffer[AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED];
            AESD_CIRCULAR_BUFFER_FOREACH(params->dataBufPtr->out_offs, params->dataBufPtr, index, &localBuffer);

            float mean = 0;
            float variance = 0;
            for(int i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++)
            {
                mean += localBuffer[i];
            }
            mean = mean / AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

            //sum of squares
            for(int j = 0; j < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; j++)
            {
                variance += ( (localBuffer[j] - mean) * (localBuffer[j] - mean) );
            }
            variance = variance / (AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - 1);
            variance = sqrt(variance);
        }
    }
}

static int packetCallback(struct nl_msg *msg, void *arg)
{
    printf("Entering callback function\n");
    threadParams* params = ((threadParams*)arg);
    
    struct nlmsghdr* msgHdr = nlmsg_hdr(msg);
    struct nlattr* msgAttr = nlmsg_find_attr(msgHdr, GENL_HDRLEN, NL80211_ATTR_STA_INFO);
    struct nlattr* dataAttr = nla_find(msgAttr, nla_len(msgAttr), NL80211_STA_INFO_BEACON_SIGNAL_AVG);
    int dataVal = nla_get_u32(dataAttr);
    params->newEntry->dBmVal = dataVal;
    add_entry(params->dataBufPtr, params->newEntry);
}