//
// Copyright (C) 2004 Andras Varga
// Copyright (C) 2009-2010 Thomas Reschka
// Copyright (C) 2010 Zoltan Bojthe
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//

#ifndef __INET_TCPCONNECTION_H
#define __INET_TCPCONNECTION_H

#include "inet/common/INETDefs.h"
#include "inet/networklayer/common/L3Address.h"
#include "inet/transportlayer/tcp/Tcp.h"
#include "inet/transportlayer/tcp_common/TcpHeader.h"
#include "inet/linklayer/ethernet/EtherMacFullDuplex.h"

namespace inet {

class TcpCommand;
class TcpOpenCommand;

namespace tcp {

class TcpHeader;
class TcpSendQueue;
class TcpSackRexmitQueue;
class TcpReceiveQueue;
class TcpAlgorithm;

//
// TCP FSM states
//
// Brief descriptions (cf RFC 793, page 20):
//
// LISTEN - waiting for a connection request
// SYN-SENT - part of 3-way handshake (waiting for peer's SYN+ACK or SYN)
// SYN-RECEIVED - part of 3-way handshake (we sent SYN too, waiting for it to be acked)
// ESTABLISHED - normal data transfer
// FIN-WAIT-1 - FIN sent, waiting for its ACK (or peer's FIN)
// FIN-WAIT-2 - our side of the connection closed (our FIN acked), waiting for peer's FIN
// CLOSE-WAIT - FIN received and acked, waiting for local user to close
// LAST-ACK - remote side closed, FIN sent, waiting for its ACK
// CLOSING - simultaneous close: sent FIN, then got peer's FIN
// TIME-WAIT - both FIN's acked, waiting for some time to be sure remote TCP received our ACK
// CLOSED - represents no connection state at all.
//
// Note: FIN-WAIT-1, FIN-WAIT-2, CLOSING, TIME-WAIT represents active close (that is,
// local user closes first), and CLOSE-WAIT and LAST-ACK represents passive close.
//
enum TcpState {
    TCP_S_INIT = 0,
    TCP_S_CLOSED = FSM_Steady(1),
    TCP_S_LISTEN = FSM_Steady(2),
    TCP_S_SYN_SENT = FSM_Steady(3),
    TCP_S_SYN_RCVD = FSM_Steady(4),
    TCP_S_ESTABLISHED = FSM_Steady(5),
    TCP_S_CLOSE_WAIT = FSM_Steady(6),
    TCP_S_LAST_ACK = FSM_Steady(7),
    TCP_S_FIN_WAIT_1 = FSM_Steady(8),
    TCP_S_FIN_WAIT_2 = FSM_Steady(9),
    TCP_S_CLOSING = FSM_Steady(10),
    TCP_S_TIME_WAIT = FSM_Steady(11)
};

//
// Event, strictly for the FSM state transition purposes.
// DO NOT USE outside performStateTransition()!
//
enum TcpEventCode {
    TCP_E_IGNORE,

    // app commands
    // (Note: no RECEIVE command, data are automatically passed up)
    TCP_E_OPEN_ACTIVE,
    TCP_E_OPEN_PASSIVE,
    TCP_E_ACCEPT,
    TCP_E_SEND,
    TCP_E_CLOSE,
    TCP_E_ABORT,
    TCP_E_DESTROY,
    TCP_E_STATUS,
    TCP_E_QUEUE_BYTES_LIMIT,
    TCP_E_READ,
    TCP_E_SETOPTION,

    // TPDU types
    TCP_E_RCV_DATA,
    TCP_E_RCV_ACK,
    TCP_E_RCV_SYN,
    TCP_E_RCV_SYN_ACK,
    TCP_E_RCV_FIN,
    TCP_E_RCV_FIN_ACK,
    TCP_E_RCV_RST,    // covers RST+ACK too

    TCP_E_RCV_UNEXP_SYN,    // unexpected SYN

    // timers
    TCP_E_TIMEOUT_2MSL,    // RFC 793, a.k.a. TIME-WAIT timer
    TCP_E_TIMEOUT_CONN_ESTAB,
    TCP_E_TIMEOUT_FIN_WAIT_2,

    // All other timers (REXMT, PERSIST, DELAYED-ACK, KEEP-ALIVE, etc.),
    // are handled in TcpAlgorithm.
};

/** @name Timeout values */
//@{
#define TCP_TIMEOUT_CONN_ESTAB        75  // 75 seconds
#define TCP_TIMEOUT_FIN_WAIT_2        600  // 10 minutes
#define TCP_TIMEOUT_SYN_REXMIT        0.01  // initially 1 seconds
#define TCP_TIMEOUT_SYN_REXMIT_MAX    240  // 4 mins (will only be used with SYN+ACK: with SYN CONN_ESTAB occurs sooner)
//@}

#define MAX_SYN_REXMIT_COUNT          12  // will only be used with SYN+ACK: with SYN CONN_ESTAB occurs sooner
#define TCP_MAX_WIN                   65535  // 65535 bytes, largest value (16 bit) for (unscaled) window size
#define DUPTHRESH                     3  // used for TcpTahoe, TcpReno and SACK (RFC 3517)
#define MAX_SACK_BLOCKS               60  // will only be used with SACK
#define PAWS_IDLE_TIME_THRESH         (24 * 24 * 3600)  // 24 days in seconds (RFC 1323)

typedef std::list<Sack> SackList;

/**
 * Contains state variables ("TCB") for TCP.
 *
 * TcpStateVariables is effectively a "struct" -- it only contains
 * public data members. (Only declared as a class so that we can use
 * cObject as base class and make it possible to inspect
 * it in Tkenv.)
 *
 * TcpStateVariables only contains variables needed to implement
 * the "base" (RFC 793) TCP. More advanced TCP variants are encapsulated
 * into TcpAlgorithm subclasses which can have their own state blocks,
 * subclassed from TcpStateVariables. See TcpAlgorithm::createStateVariables().
 */
class INET_API TcpStateVariables : public cObject
{
  public:
    TcpStateVariables();
    virtual std::string str() const override;
    virtual std::string detailedInfo() const OMNETPP5_CODE(override);

  public:
    bool FRs_disabled;

    bool active;    // set if the connection was initiated by an active open
    bool fork;    // if passive and in LISTEN: whether to fork on an incoming connection

    uint32 snd_mss;    // sender maximum segment size (without headers, i.e. only segment text); see RFC 2581, page 1.
                       // This will be set to the minimum of the local smss parameter and the value specified in the
                       // MSS option received during connection setup.

    // send sequence number variables (see RFC 793, "3.2. Terminology")
    uint32 snd_una;    // send unacknowledged
    uint32 snd_nxt;    // send next (drops back on retransmission)
    uint32 snd_max;    // max seq number sent (needed because snd_nxt is re-set on retransmission)
    uint32 snd_wnd;    // send window
    uint32 snd_up;    // send urgent pointer
    uint32 snd_wl1;    // segment sequence number used for last window update
    uint32 snd_wl2;    // segment ack. number used for last window update
    uint32 iss;    // initial sequence number (ISS)
    simtime_t iss_initialized_time; // used for flow rate at bolt

    // receive sequence number variables
    uint32 rcv_nxt;    // receive next
    uint32 rcv_wnd;    // receive window
    uint32 rcv_up;    // receive urgent pointer;
    uint32 irs;    // initial receive sequence number
    uint32 rcv_adv;    // advertised window

    // SYN, SYN+ACK retransmission variables (handled separately
    // because normal rexmit belongs to TcpAlgorithm)
    int syn_rexmit_count;    // number of SYN/SYN+ACK retransmissions (=1 after first rexmit)
    simtime_t syn_rexmit_timeout;    // current SYN/SYN+ACK retransmission timeout

    // whether ACK of our FIN has been received. Needed in FIN bit processing
    // to decide between transition to TIME-WAIT and CLOSING (set event code
    // TCP_E_RCV_FIN or TCP_E_RCV_FIN_ACK).
    bool fin_ack_rcvd;

    bool send_fin;    // true if a user CLOSE command has been "queued"
    uint32 snd_fin_seq;    // if send_fin == true: FIN should be sent just before this sequence number

    bool fin_rcvd;    // whether FIN received or not
    uint32 rcv_fin_seq;    // if fin_rcvd: sequence number of received FIN

    uint32 sentBytes;    // amount of user data (in bytes) sent in last segment

    bool nagle_enabled;    // set if Nagle's algorithm (RFC 896) is enabled
    bool delayed_acks_enabled;    // set if delayed ACK algorithm (RFC 1122) is enabled
    bool limited_transmit_enabled;    // set if Limited Transmit algorithm (RFC 3042) is enabled
    bool increased_IW_enabled;    // set if Increased Initial Window (RFC 3390) is enabled

    //custom IW setting
    bool use_custom_IW;
    int custom_IW_mult;

    uint32 full_sized_segment_counter;    // this counter is needed for delayed ACK
    bool ack_now;    // send ACK immediately, needed if delayed_acks_enabled is set
                     // Based on [Stevens, W.R.: TCP/IP Illustrated, Volume 2, page 861].
                     // ack_now should be set when:
                     //   - delayed ACK timer expires
                     //   - an out-of-order segment is received
                     //   - SYN is received during the three-way handshake
                     //   - a persist probe is received
                     //   - FIN is received

    bool afterRto;    // set at RTO, reset when snd_nxt == snd_max or snd_una == snd_max

    // WINDOW_SCALE related variables
    bool ws_support;    // set if the host supports Window Scale (header option) (RFC 1322)
    bool ws_enabled;    // set if the connection uses Window Scale (header option)
    int  ws_manual_scale; // the value of scale parameter if it was set manually (-1 otherwise)
    bool snd_ws;    // set if initial WINDOW_SCALE has been sent
    bool rcv_ws;    // set if initial WINDOW_SCALE has been received
    uint rcv_wnd_scale;    // RFC 1323, page 31: "Receive window scale power"
    uint snd_wnd_scale;    // RFC 1323, page 31: "Send window scale power"

    // TIMESTAMP related variables
    bool ts_support;    // set if the host supports Timestamps (header option) (RFC 1322)
    bool ts_enabled;    // set if the connection uses Window Scale (header option)
    bool snd_initial_ts;    // set if initial TIMESTAMP has been sent
    bool rcv_initial_ts;    // set if initial TIMESTAMP has been received
    simtime_t ts_recent;    // RFC 1323, page 31: "Latest received Timestamp"
    uint32 last_ack_sent;    // RFC 1323, page 31: "Last ACK field sent"
    simtime_t time_last_data_sent;    // time at which the last data segment was sent (needed to compute the IDLE time for PAWS)

    // SACK related variables
    bool sack_support;    // set if the host supports selective acknowledgment (header option) (RFC 2018, 2883, 3517)
    bool sack_enabled;    // set if the connection uses selective acknowledgment (header option)
    bool snd_sack_perm;    // set if SACK_PERMITTED has been sent
    bool rcv_sack_perm;    // set if SACK_PERMITTED has been received
    uint32 start_seqno;    // start sequence number of last received out-of-order segment
    uint32 end_seqno;    // end sequence number of last received out-of-order segment
    bool snd_sack;    // set if received vaild out-of-order segment or rcv_nxt changed, but receivedQueue is not empty
    bool snd_dsack;    // set if received duplicated segment (sequenceNo+PLength < rcv_nxt) or (segment is not acceptable)
    SackList sacks_array;    // MAX_SACK_BLOCKS is set to 60
    uint32 highRxt;    // RFC 3517, page 3: ""HighRxt" is the highest sequence number which has been retransmitted during the current loss recovery phase."
    uint32 pipe;    // RFC 3517, page 3: ""Pipe" is a sender's estimate of the number of bytes outstanding in the network."
    uint32 recoveryPoint;    // RFC 3517
    uint32 sackedBytes;    // number of sackedBytes
    uint32 sackedBytes_old;    // old number of sackedBytes - needed for RFC 3042 to check if last dupAck contained new sack information
    bool lossRecovery;    // indicates if algorithm is in loss recovery phase

    // queue management
    uint32 sendQueueLimit;
    bool queueUpdate;

    // those counters would logically belong to TcpAlgorithm, but it's a lot easier to manage them here
    uint32 dupacks;    // current number of received consecutive duplicate ACKs
    uint32 snd_sacks;    // number of sent sacks
    uint32 rcv_sacks;    // number of received sacks
    uint32 rcv_oooseg;    // number of received out-of-order segments
    uint32 rcv_naseg;    // number of received not acceptable segments

    // receiver buffer / receiver queue related variables
    uint32 maxRcvBuffer;    // maximal amount of bytes in tcp receive queue
    uint32 usedRcvBuffer;    // current amount of used bytes in tcp receive queue
    uint32 freeRcvBuffer;    // current amount of free bytes in tcp receive queue
    uint32 tcpRcvQueueDrops;    // number of drops in tcp receive queue

    //ECN
    bool ecnEchoState;         // indicates if connection is in echo mode (got CE indication from IP and didn't get CWR from sender yet)
    bool sndCwr;               // set if ECE was handled
    bool gotEce;               // set if packet with ECE arrived
    bool gotCeIndication;      // set if CE was set in controlInfo from IP
    bool ect;                  // set if this connection is ECN Capable (ECT stands for ECN-Capable transport - rfc-3168)
    bool endPointIsWillingECN; // set if the other end-point is willing to use ECN
    bool ecnSynSent;           // set if ECN-setup SYN packet was sent
    bool ecnWillingness;       // set if current host is willing to use ECN
    bool sndAck;               // set if sending Ack packet, used to set relevant info in controlInfo.
    bool rexmit;               // set if retransmitting data, used to send not-ECT codepoint (rfc3168, p. 20)
    simtime_t eceReactionTime; // records the time of the last ECE reaction

    //DCTCP
    uint32 DCTCPWindowEnd;     //the TCP sequence number threshold when one observation window ends and another is to begin
    uint32 DCTCPBytesAcked;     //the number of sent bytes acknowledged during the current observation window
    uint32 DCTCPBytesMarked;    //the number of bytes sent during the current observation window that encountered congestion
    float DCTCPAlpha;

    // Swift
    // cwnds are per packet and are transmitted to per byte at last
    double ai;     // fabric additive increament
    double beta;       // fabric multiplicative decrease constant
    double max_mdf;        // fabric maximum multiplicative decrease factor
    simtime_t end_point_delay;
    simtime_t fabric_delay;
    int hop_count;
    simtime_t base_target_delay;    // base target delay
    double per_hop_scaling_factor;  // h: per hop scaling factor
    double fs_max_cwnd;     // max cwnd for target scaling
    double fs_min_cwnd;     // min cwnd for target scaling
    double fs_range;        // max scaling range
    simtime_t constant_endpoint_target_delay;   // for ecwnd we use constant target delay
    double endpoint_EWMA_factor;   // for ecwnd we use constant target delay

    //pFabric
    bool in_probe_mode = false;

    // bolt
    uint32 seq_no_at_last_AI = 0;
    Packet* last_ack_packet_rcvd;
    bool last_data_packet_inc = false;
    double server_link_rate;
};

/**
 * Manages a TCP connection. This class itself implements the TCP state
 * machine as well as handling control PDUs (SYN, SYN+ACK, RST, FIN, etc.),
 * timers (2MSL, CONN-ESTAB, FIN-WAIT-2) and events (OPEN, SEND, etc)
 * associated with TCP state changes.
 *
 * The implementation largely follows the functional specification at the end
 * of RFC 793. Code comments extensively quote RFC 793 to make it easier
 * to understand.
 *
 * TcpConnection objects are not used alone -- they are instantiated and managed
 * by a TCP module.
 *
 * TcpConnection "outsources" several tasks to objects subclassed from
 * TcpSendQueue, TcpReceiveQueue and TcpAlgorithm; see overview of this
 * with TCP documentation.
 *
 * Connection variables (TCB) are kept in TcpStateVariables. TcpAlgorithm
 * implementations can extend TcpStateVariables to add their own stuff
 * (see TcpAlgorithm::createStateVariables() factory method.)
 *
 * The "entry points" of TCPConnnection from TCP are:
 *  - processTimer(cMessage *msg): handle self-messages which belong to the connection
 *  - processTCPSegment(TcpHeader *tcpSeg, Address srcAddr, Address destAddr):
 *    handle segment arrivals
 *  - processAppCommand(cMessage *msg): process commands which arrive from the
 *    application (TCP_C_xxx)
 *
 * All three methods follow a common structure:
 *
 *  -# dispatch to specific methods. For example, processAppCommand() invokes
 *     one of process_OPEN_ACTIVE(), process_OPEN_PASSIVE() or process_SEND(),
 *     etc., and processTCPSegment() dispatches to processSegmentInListen(),
 *     processSegmentInSynSent() or processSegment1stThru8th().
 *     Those methods will do the REAL JOB.
 *  -# after they return, we'll know the state machine event (TcpEventCode,
 *     TCP_E_xxx) for sure, so we can:
 *  -# invoke performStateTransition() which executes the necessary state
 *     transition (for example, TCP_E_RCV_SYN will take the state machine
 *     from TCP_S_LISTEN to TCP_S_SYN_RCVD). No other actions are taken
 *     in this step.
 *  -# if there was a state change (for example, we entered the
 *     TCP_S_ESTABLISHED state), performStateTransition() invokes stateEntered(),
 *     which performs some necessary housekeeping (cancel the CONN-ESTAB timer).
 *
 * When the CLOSED state is reached, TCP will delete the TcpConnection object.
 *
 */
class INET_API TcpConnection : public cSimpleModule
{
  public:
    static simsignal_t tcpConnectionAddedSignal;
    static simsignal_t stateSignal;    // FSM state
    static simsignal_t sndWndSignal;    // snd_wnd
    static simsignal_t rcvWndSignal;    // rcv_wnd
    static simsignal_t rcvAdvSignal;    // current advertised window (=rcv_adv)
    static simsignal_t sndNxtSignal;    // sent seqNo
    static simsignal_t sndAckSignal;    // sent ackNo
    static simsignal_t rcvSeqSignal;    // received seqNo
    static simsignal_t rcvAckSignal;    // received ackNo (=snd_una)
    static simsignal_t unackedSignal;    // number of bytes unacknowledged
    static simsignal_t dupAcksSignal;    // current number of received dupAcks
    static simsignal_t pipeSignal;    // current sender's estimate of bytes outstanding in the network
    static simsignal_t sndSacksSignal;    // number of sent Sacks
    static simsignal_t rcvSacksSignal;    // number of received Sacks
    static simsignal_t rcvOooSegSignal;    // number of received out-of-order segments
    static simsignal_t rcvNASegSignal;    // number of received not acceptable segments
    static simsignal_t sackedBytesSignal;    // current number of received sacked bytes
    static simsignal_t tcpRcvQueueBytesSignal;    // current amount of used bytes in tcp receive queue
    static simsignal_t tcpRcvQueueDropsSignal;    // number of drops in tcp receive queue
    static simsignal_t retransmittedSignal; // number of retransmitted segments

    // v2 marking and ordering
    std::string source_ip, destination_ip, source_port, destination_port;
    bool connection_information_stored = false;
    void store_connection_info(const Ptr<const TcpHeader>& tcpseg,
            L3Address src, L3Address dest);
    void notify_ip_of_connection_closed(bool close_immediately=false);

    // connection identification by apps: socketId
    int socketId = -1;    // identifies connection within the app
    int getSocketId() const { return socketId; }
    void setSocketId(int newSocketId) { ASSERT(socketId == -1); socketId = newSocketId; }

    int listeningSocketId = -1;    // identifies listening connection within the app
    int getListeningSocketId() const { return listeningSocketId; }

    // socket pair
    L3Address localAddr;
    const L3Address& getLocalAddr() const { return localAddr; }
    L3Address remoteAddr;
    const L3Address& getRemoteAddr() const { return remoteAddr; }
    int localPort = -1;
    int remotePort = -1;

    // TCP options for this connection
    int ttl = -1;
    short dscp = -1;
    short tos = -1;
    Tcp *tcpMain = nullptr;    // Tcp module

    // swift
    simtime_t rcvd_time_to_be_echod = 0;
    unsigned int hop_count_to_be_echod = 0;

  protected:

    // TCP state machine
    cFSM fsm;

    // variables associated with TCP state
    TcpStateVariables *state = nullptr;

    // TCP queues
    TcpSendQueue *sendQueue = nullptr;
    TcpSendQueue *getSendQueue() const { return sendQueue; }
    TcpReceiveQueue *receiveQueue = nullptr;
    TcpReceiveQueue *getReceiveQueue() const { return receiveQueue; }

  public:
    TcpSackRexmitQueue *rexmitQueue = nullptr;
    TcpSackRexmitQueue *getRexmitQueue() const { return rexmitQueue; }
    ulong get_bytes_available_in_tcp_queue();

  protected:
    // TCP behavior in data transfer state
    TcpAlgorithm *tcpAlgorithm = nullptr;
    TcpAlgorithm *getTcpAlgorithm() const { return tcpAlgorithm; }

    // timers
    cMessage *the2MSLTimer = nullptr;
    cMessage *connEstabTimer = nullptr;
    cMessage *finWait2Timer = nullptr;
    cMessage *synRexmitTimer = nullptr;    // for retransmitting SYN and SYN+ACK

    bool is_bursty = false, is_bursty_indicated = false;

  protected:
    /** @name FSM transitions: analysing events and executing state transitions */
    //@{
    /** Maps app command codes (msg kind of app command msgs) to TCP_E_xxx event codes */
    virtual TcpEventCode preanalyseAppCommandEvent(int commandCode);
    /** Implemements the pure TCP state machine */
    virtual bool performStateTransition(const TcpEventCode& event);
    /** Perform cleanup necessary when entering a new state, e.g. cancelling timers */
    virtual void stateEntered(int state, int oldState, TcpEventCode event);
    //@}

    /** @name Processing app commands. Invoked from processAppCommand(). */
    //@{
    virtual void process_OPEN_ACTIVE(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg);
    virtual void process_OPEN_PASSIVE(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg);
    virtual void process_ACCEPT(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg);
    virtual void process_SEND(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg);
    virtual void process_CLOSE(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg);
    virtual void process_ABORT(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg);
    virtual void process_DESTROY(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg);
    virtual void process_STATUS(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg);
    virtual void process_QUEUE_BYTES_LIMIT(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg);
    virtual void process_READ_REQUEST(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg);
    virtual void process_OPTIONS(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg);
    //@}

    /** @name Processing TCP segment arrivals. Invoked from processTCPSegment(). */
    //@{
    /**
     * Shortcut to process most common case as fast as possible. Returns false
     * if segment requires normal (slow) route.
     */
    virtual bool tryFastRoute(const Ptr<const TcpHeader>& tcpseg);
    /**
     * Process incoming TCP segment. Returns a specific event code (e.g. TCP_E_RCV_SYN)
     * which will drive the state machine.
     */
    virtual TcpEventCode process_RCV_SEGMENT(Packet *packet, const Ptr<const TcpHeader>& tcpseg, L3Address src, L3Address dest);
    virtual TcpEventCode processSegmentInListen(Packet *packet, const Ptr<const TcpHeader>& tcpseg, L3Address src, L3Address dest);
    virtual TcpEventCode processSynInListen(Packet *packet, const Ptr<const TcpHeader>& tcpseg, L3Address srcAddr, L3Address destAddr);
    virtual TcpEventCode processSegmentInSynSent(Packet *packet, const Ptr<const TcpHeader>& tcpseg, L3Address src, L3Address dest);
    virtual TcpEventCode processSegment1stThru8th(Packet *packet, const Ptr<const TcpHeader>& tcpseg, L3Address src, L3Address dests);
    virtual TcpEventCode processRstInSynReceived(const Ptr<const TcpHeader>& tcpseg);
    virtual bool processAckInEstabEtc(Packet *packet, const Ptr<const TcpHeader>& tcpseg);
    //@}

    /** @name Processing of TCP options. Invoked from readHeaderOptions(). Return value indicates whether the option was valid. */
    //@{
    virtual bool processMSSOption(const Ptr<const TcpHeader>& tcpseg, const TcpOptionMaxSegmentSize& option);
    virtual bool processWSOption(const Ptr<const TcpHeader>& tcpseg, const TcpOptionWindowScale& option);
    virtual bool processSACKPermittedOption(const Ptr<const TcpHeader>& tcpseg, const TcpOptionSackPermitted& option);
    virtual bool processSACKOption(const Ptr<const TcpHeader>& tcpseg, const TcpOptionSack& option);
    virtual bool processTSOption(const Ptr<const TcpHeader>& tcpseg, const TcpOptionTimestamp& option);
    //@}

    /** @name Processing timeouts. Invoked from processTimer(). */
    //@{
    virtual void process_TIMEOUT_2MSL();
    virtual void process_TIMEOUT_CONN_ESTAB();
    virtual void process_TIMEOUT_FIN_WAIT_2();
    virtual void process_TIMEOUT_SYN_REXMIT(TcpEventCode& event);
    //@}

    /** Utility: clone a listening connection. Used for forking. */
    virtual TcpConnection *cloneListeningConnection();

    virtual void initClonedConnection(TcpConnection *listenerConn);

    /** Utility: creates send/receive queues and tcpAlgorithm */
    virtual void initConnection(TcpOpenCommand *openCmd);

    /** Utility: set snd_mss, rcv_wnd and sack in newly created state variables block */
    virtual void configureStateVariables();

    /** Utility: generates ISS and initializes corresponding state variables */
    virtual void selectInitialSeqNum();

    /** Utility: check if segment is acceptable (all bytes are in receive window) */
    virtual bool isSegmentAcceptable(Packet *packet, const Ptr<const TcpHeader>& tcpseg) const;

    /** Utility: send SYN */
    virtual void sendSyn();

    /** Utility: send SYN+ACK */
    virtual void sendSynAck();

    /** Utility: readHeaderOptions (Currently only EOL, NOP, MSS, WS, SACK_PERMITTED, SACK and TS are implemented) */
    virtual void readHeaderOptions(const Ptr<const TcpHeader>& tcpseg);

    /** Utility: writeHeaderOptions (Currently only EOL, NOP, MSS, WS, SACK_PERMITTED, SACK and TS are implemented) */
    virtual TcpHeader writeHeaderOptions(const Ptr<TcpHeader>& tcpseg);

    /** Utility: adds SACKs to segments header options field */
    virtual TcpHeader addSacks(const Ptr<TcpHeader>& tcpseg);

    /** Utility: get TSval from segments TS header option */
    virtual simtime_t getTSval(const Ptr<const TcpHeader>& tcpseg) const;

    /** Utility: get TSecr from segments TS header option */
    virtual simtime_t getTSecr(const Ptr<const TcpHeader>& tcpseg) const;

    /** Utility: returns true if the connection is not yet accepted by the application */
    virtual bool isToBeAccepted() const { return listeningSocketId != -1; }

  public:
    /** Utility: send ACK */
    virtual void sendAck(simtime_t receive_time_to_be_echoed = 0, unsigned int hop_count_to_be_echoed = 0);

    /**
     * Utility: Send data from sendQueue, at most congestionWindow.
     * If fullSegmentsOnly is set, don't send segments smaller than SMSS (needed for Nagle).
     * Returns true if some data was actually sent.
     */
    virtual bool sendData(bool fullSegmentsOnly, uint32 congestionWindow);

    /** Utility: sends 1 bytes as "probe", called by the "persist" mechanism */
    virtual bool sendProbe();

    /** Utility: retransmit one segment from snd_una */
    virtual void retransmitOneSegment(bool called_at_rto);

    /** Utility: retransmit all from snd_una to snd_max */
    virtual void retransmitData();

    /** Utility: sends RST */
    virtual void sendRst(uint32 seqNo);
    /** Utility: sends RST; does not use connection state */
    virtual void sendRst(uint32 seq, L3Address src, L3Address dest, int srcPort, int destPort);
    /** Utility: sends RST+ACK; does not use connection state */
    virtual void sendRstAck(uint32 seq, uint32 ack, L3Address src, L3Address dest, int srcPort, int destPort);

    /** Utility: sends FIN */
    virtual void sendFin();

    /**
     * Utility: sends one segment of 'bytes' bytes from snd_nxt, and advances snd_nxt.
     * sendData(), sendProbe() and retransmitData() internally all rely on this one.
     */
    virtual void sendSegment(uint32 bytes);

    /** Utility: adds control info to segment and sends it to IP */
    virtual void sendToIP(Packet *packet, const Ptr<TcpHeader>& tcpseg);

    /** Utility: start SYN-REXMIT timer */
    virtual void startSynRexmitTimer();

    /** Utility: signal to user that connection timed out */
    virtual void signalConnectionTimeout();

    /** Utility: start a timer */
    void scheduleTimeout(cMessage *msg, simtime_t timeout) { scheduleAt(simTime() + timeout, msg); }

  protected:
    /** Utility: cancel a timer */
    // cMessage *cancelEvent(cMessage *msg) { return tcpMain->cancelEvent(msg); }

    /** Utility: send IP packet */
    virtual void sendToIP(Packet *pkt, const Ptr<TcpHeader>& tcpseg, L3Address src, L3Address dest);

    /** Utility: sends packet to application */
    virtual void sendToApp(cMessage *msg);

    /** Utility: sends status indication (TCP_I_xxx) to application */
    virtual void sendIndicationToApp(int code, const int id = 0);

    /** Utility: sends TCP_I_AVAILABLE indication with TcpAvailableInfo to application */
    virtual void sendAvailableIndicationToApp();

    /** Utility: sends TCP_I_ESTABLISHED indication with TcpConnectInfo to application */
    virtual void sendEstabIndicationToApp();

    /** Utility: sends data or data notification to application */
    virtual void sendAvailableDataToApp();

  public:
    /** Utility: prints local/remote addr/port and app gate index/socketId */
    virtual void printConnBrief() const;
    /** Utility: prints important header fields */
    static void printSegmentBrief(Packet *packet, const Ptr<const TcpHeader>& tcpseg);
    /** Utility: returns name of TCP_S_xxx constants */
    static const char *stateName(int state);
    /** Utility: returns name of TCP_E_xxx constants */
    static const char *eventName(int event);
    /** Utility: returns name of TCP_I_xxx constants */
    static const char *indicationName(int code);
    /** Utility: returns name of TCPOPTION_xxx constants */
    static const char *optionName(int option);
    /** Utility: update receiver queue related variables and statistics - called before setting rcv_wnd */
    virtual void updateRcvQueueVars();

    /** Utility: returns true when receive queue has enough space for store the tcpseg */
    virtual bool hasEnoughSpaceForSegmentInReceiveQueue(Packet *packet, const Ptr<const TcpHeader>& tcpseg);

    /** Utility: update receive window (rcv_wnd), and calculate scaled value if window scaling enabled.
     *  Returns the (scaled) receive window size.
     */
    virtual unsigned short updateRcvWnd();

    /** Utility: update window information (snd_wnd, snd_wl1, snd_wl2) */
    virtual void updateWndInfo(const Ptr<const TcpHeader>& tcpseg, bool doAlways = false);

  public:
    TcpConnection() {}
    TcpConnection(const TcpConnection& other) {}    //FIXME kludge
    void initialize() {}

    /**
     * The "normal" constructor.
     */
    void initConnection(Tcp *mod, int socketId);

    /**
     * Destructor.
     */
    virtual ~TcpConnection();

    int getLocalPort() const { return localPort; }
    L3Address getLocalAddress() const { return localAddr; }

    int getRemotePort() const { return remotePort; }
    L3Address getRemoteAddress() const { return remoteAddr; }

    /**
     * This method gets invoked from TCP when a segment arrives which
     * doesn't belong to an existing connection. TCP creates a temporary
     * connection object so that it can call this method, then immediately
     * deletes it.
     */
    virtual void segmentArrivalWhileClosed(Packet *packet, const Ptr<const TcpHeader>& tcpseg, L3Address src, L3Address dest);

    /** @name Various getters **/
    //@{
    int getFsmState() const { return fsm.getState(); }
    const TcpStateVariables *getState() const { return state; }
    TcpStateVariables *getState() { return state; }
    TcpSendQueue *getSendQueue() { return sendQueue; }
    TcpSackRexmitQueue *getRexmitQueue() { return rexmitQueue; }
    TcpReceiveQueue *getReceiveQueue() { return receiveQueue; }
    TcpAlgorithm *getTcpAlgorithm() { return tcpAlgorithm; }
    Tcp *getTcpMain() { return tcpMain; }
    //@}

    /**
     * Process self-messages (timers).
     * Normally returns true. A return value of false means that the
     * connection structure must be deleted by the caller (TCP).
     */
    bool flush_segment();
    virtual bool processTimer(cMessage *msg);

    /**
     * Process incoming TCP segment. Normally returns true. A return value
     * of false means that the connection structure must be deleted by the
     * caller (TCP).
     */
    bool sort_and_check_packets_from_gro();
    bool pop_appropriate_packets_from_gro();
    virtual bool processTCPSegment(Packet *packet, const Ptr<const TcpHeader>& tcpseg, L3Address srcAddr, L3Address destAddr);

    /**
     * Process commands from the application.
     * Normally returns true. A return value of false means that the
     * connection structure must be deleted by the caller (TCP).
     */
    virtual bool processAppCommand(cMessage *msg);

    virtual void handleMessage(cMessage *msg);

    /**
     * For SACK TCP. RFC 3517, page 3: "This routine returns whether the given
     * sequence number is considered to be lost.  The routine returns true when
     * either DupThresh discontiguous SACKed sequences have arrived above
     * 'SeqNum' or (DupThresh * SMSS) bytes with sequence numbers greater
     * than 'SeqNum' have been SACKed.  Otherwise, the routine returns
     * false."
     */
    virtual bool isLost(uint32 seqNum);

    /**
     * For SACK TCP. RFC 3517, page 3: "This routine traverses the sequence
     * space from HighACK to HighData and MUST set the "pipe" variable to an
     * estimate of the number of octets that are currently in transit between
     * the TCP sender and the TCP receiver."
     */
    virtual void setPipe();

    /**
     * For SACK TCP. RFC 3517, page 3: "This routine uses the scoreboard data
     * structure maintained by the Update() function to determine what to transmit
     * based on the SACK information that has arrived from the data receiver
     * (and hence been marked in the scoreboard).  NextSeg () MUST return the
     * sequence number range of the next segment that is to be
     * transmitted..."
     * Returns true if a valid sequence number (for the next segment) is found and
     * returns false if no segment should be send.
     */
    virtual bool nextSeg(uint32& seqNum);

    /**
     * Utility: send data during Loss Recovery phase (if SACK is enabled).
     */
    virtual void sendDataDuringLossRecoveryPhase(uint32 congestionWindow);

    /**
     * Utility: send segment during Loss Recovery phase (if SACK is enabled).
     */
    virtual void sendSegmentDuringLossRecoveryPhase(uint32 seqNum);

    /**
     * Utility: send one new segment from snd_max if allowed (RFC 3042).
     */
    virtual void sendOneNewSegment(bool fullSegmentsOnly, uint32 congestionWindow);

    /**
     * Utility: converts a given simtime to a timestamp (TS).
     */
    static uint32 convertSimtimeToTS(simtime_t simtime);

    /**
     * Utility: converts a given timestamp (TS) to a simtime.
     */
    static simtime_t convertTSToSimtime(uint32 timestamp);

    /**
     * Utility: checks if send queue is empty (no data to send).
     */
    virtual bool isSendQueueEmpty();
};

} // namespace tcp

} // namespace inet

#endif // ifndef __INET_TCPCONNECTION_H
