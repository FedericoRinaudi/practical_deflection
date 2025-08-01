//
// Copyright (C) 2004 Andras Varga
// Copyright (C) 2009-2010 Thomas Reschka
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

#ifndef __INET_TCPBASEALG_H
#define __INET_TCPBASEALG_H

#include "inet/common/INETDefs.h"
#include "inet/transportlayer/tcp/TcpAlgorithm.h"

namespace inet {
namespace tcp {

/**
 * State variables for TcpBaseAlg.
 */
class INET_API TcpBaseAlgStateVariables : public TcpStateVariables
{
  public:
    TcpBaseAlgStateVariables();
    virtual std::string str() const override;
    virtual std::string detailedInfo() const override;

    /// retransmit count
    //@{
    int rexmit_count;    ///< number of retransmissions (=1 after first rexmit)
    simtime_t rexmit_timeout;    ///< current retransmission timeout (aka RTO)
    //@}

    /// persist factor
    //@{
    uint persist_factor;    ///< factor needed for simplified PERSIST timer calculation
    simtime_t persist_timeout;    ///< current persist timeout
    //@}

    /// congestion window
    //@{
    uint32 snd_cwnd;    ///< congestion window
    //@}

    /// round-trip time measurements
    //@{
    uint32 rtseq;    ///< starting sequence number of timed data
    simtime_t rtseq_sendtime;    ///< time when rtseq was sent (0 if RTT measurement is not running)
    //@}

    /// round-trip time estimation (Jacobson's algorithm)
    //@{
    simtime_t srtt;    ///< smoothed round-trip time
    simtime_t rttvar;    ///< variance of round-trip time
    //@}

    /// number of RTOs
    //@{
    uint32 numRtos;    ///< total number of RTOs
    //@}

    /// RFC 3782 variables
    //@{
    uint32 recover;    ///< recover (RFC 3782)
    bool firstPartialACK;    ///< first partial acknowledgement (RFC 3782)
    //@}
};

/**
 * Includes basic TCP algorithms: adaptive retransmission, PERSIST timer,
 * keep-alive, delayed acks -- EXCLUDING congestion control. Congestion
 * control is implemented in subclasses such as TCPTahoeAlg or TCPRenoAlg.
 *
 * Implements:
 *   - delayed ACK algorithm (RFC 1122)
 *   - Jacobson's and Karn's algorithms for adaptive retransmission
 *   - Nagle's algorithm (RFC 896) to prevent silly window syndrome
 *   - Increased Initial Window (RFC 3390)
 *   - PERSIST timer
 *
 * To be done:
 *   - KEEP-ALIVE timer
 *
 * Note: currently the timers and time calculations are done in double
 * and NOT in Unix (200ms or 500ms) ticks. It's possible to write another
 * TcpAlgorithm which uses ticks (or rather, factor out timer handling to
 * separate methods, and redefine only those).
 *
 * Congestion window is set to SMSS when the connection is established,
 * and not touched after that. Subclasses may redefine any of the virtual
 * functions here to add their congestion control code.
 */
class INET_API TcpBaseAlg : public TcpAlgorithm
{
  protected:
    TcpBaseAlgStateVariables *& state;    // alias to TcpAlgorithm's 'state'

    cMessage *rexmitTimer;
    cMessage *persistTimer;
    cMessage *delayedAckTimer;
    cMessage *keepAliveTimer;
    cMessage *pacingTimer;

    static simsignal_t cwndSignal;    // will record changes to snd_cwnd
    static simsignal_t ssthreshSignal;    // will record changes to ssthresh
    static simsignal_t rttSignal;    // will record measured RTT
    static simsignal_t srttSignal;    // will record smoothed RTT
    static simsignal_t rttvarSignal;    // will record RTT variance (rttvar)
    static simsignal_t rtoSignal;    // will record retransmission timeout
    static simsignal_t numRtosSignal;    // will record total number of RTOs
    static simsignal_t seqNoSignal;    // will record sequence number

  protected:
    /** @name Process REXMIT, PERSIST, DELAYED-ACK and KEEP-ALIVE timers */
    //@{
    virtual void processRexmitTimer(TcpEventCode& event);
    virtual void processPersistTimer(TcpEventCode& event);
    virtual void processDelayedAckTimer(TcpEventCode& event);
    virtual void processKeepAliveTimer(TcpEventCode& event);
    virtual void processPacingTimer(TcpEventCode& event, int timer_type);
    //@}

    /**
     * Start REXMIT timer and initialize retransmission variables
     */
    virtual void startRexmitTimer();

    /**
     * Update state vars with new measured RTT value. Passing two simtime_t's
     * will allow rttMeasurementComplete() to do calculations in double or
     * in 200ms/500ms ticks, as needed)
     */
    virtual void rttMeasurementComplete(simtime_t tSent, simtime_t tAcked);

    /**
     * Converting uint32 echoedTS to simtime_t and calling rttMeasurementComplete()
     * to update state vars with new measured RTT value.
     */
    virtual void rttMeasurementCompleteUsingTS(simtime_t echoedTS) override;

    /**
     * Send data, observing Nagle's algorithm and congestion window
     */
    virtual bool sendData(bool sendCommandInvoked);

    /** Utility function */
    cMessage *cancelEvent(cMessage *msg) { return conn->cancelEvent(msg); }

  public:
    /**
     * Ctor.
     */
    TcpBaseAlg();

    /**
     * Virtual dtor.
     */
    virtual ~TcpBaseAlg();

    /**
     * Create timers, etc.
     */
    virtual void initialize() override;

    virtual void established(bool active) override;

    virtual void connectionClosed() override;

    /**
     * Process REXMIT, PERSIST, DELAYED-ACK and KEEP-ALIVE timers.
     */
    virtual void processTimer(cMessage *timer, TcpEventCode& event) override;

    virtual void sendCommandInvoked() override;

    virtual void receivedOutOfOrderSegment() override;

    virtual void receiveSeqChanged() override;

    virtual void receivedDataAck(uint32 firstSeqAcked) override;

    virtual void receivedDuplicateAck() override;

    virtual void receivedAckForDataNotYetSent(uint32 seq) override;

    virtual void ackSent() override;

    virtual void dataSent(uint32 fromseq) override;

    virtual void segmentRetransmitted(uint32 fromseq, uint32 toseq) override;

    virtual void restartRexmitTimer() override;
};

} // namespace tcp
} // namespace inet

#endif // ifndef __INET_TCPBASEALG_H

