//
// Copyright (C) 2004 Andras Varga
// Copyright (C) 2009-2011 Thomas Reschka
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

#include <string.h>

#include "inet/networklayer/common/EcnTag_m.h"
#include "inet/transportlayer/contract/tcp/TcpCommand_m.h"
#include "inet/transportlayer/tcp/Tcp.h"
#include "inet/transportlayer/tcp/TcpAlgorithm.h"
#include "inet/transportlayer/tcp/TcpConnection.h"
#include "inet/transportlayer/tcp/TcpReceiveQueue.h"
#include "inet/transportlayer/tcp/TcpSackRexmitQueue.h"
#include "inet/transportlayer/tcp/TcpSendQueue.h"
#include "inet/transportlayer/tcp_common/TcpHeader.h"
#include "inet/networklayer/ipv4/Ipv4Header_m.h"
#include "inet/transportlayer/tcp_common/TcpHeader_m.h"
#include "inet/linklayer/ethernet/EtherFrame_m.h"
#include "inet/linklayer/ethernet/EtherPhyFrame_m.h"
#include "inet/applications/tcpapp/GenericAppMsg_m.h"
#include "inet/common/FlowKey.h"

namespace inet {
namespace tcp {

bool TcpConnection::tryFastRoute(const Ptr<const TcpHeader>& tcpseg)
{
    // fast route processing not yet implemented
    return false;
}

void TcpConnection::segmentArrivalWhileClosed(Packet *packet, const Ptr<const TcpHeader>& tcpseg, L3Address srcAddr, L3Address destAddr)
{
    EV_INFO << "Seg arrived: ";
    printSegmentBrief(packet, tcpseg);

    // This segment doesn't belong to any connection, so this object
    // must be a temp object created solely for the purpose of calling us

    ASSERT(state == nullptr);

    EV_INFO << "Segment doesn't belong to any existing connection\n";

    // RFC 793:
    //"
    // all data in the incoming segment is discarded.  An incoming
    // segment containing a RST is discarded.  An incoming segment not
    // containing a RST causes a RST to be sent in response.  The
    // acknowledgment and sequence field values are selected to make the
    // reset sequence acceptable to the TCP that sent the offending
    // segment.
    //
    // If the ACK bit is off, sequence number zero is used,
    //
    //    <SEQ=0><ACK=SEG.SEQ+SEG.LEN><CTL=RST,ACK>
    //
    // If the ACK bit is on,
    //
    //    <SEQ=SEG.ACK><CTL=RST>
    //
    // ...
    //
    //    SEG.LEN = the number of octets occupied by the data in the segment
    //              (counting SYN and FIN)
    //"
    if (tcpseg->getRstBit()) {
        EV_DETAIL << "RST bit set: dropping segment\n";
        return;
    }

    if (!tcpseg->getAckBit()) {
        EV_DETAIL << "ACK bit not set: sending RST+ACK\n";
        uint32 ackNo = tcpseg->getSequenceNo() + packet->getByteLength() - B(tcpseg->getHeaderLength()).get() + tcpseg->getSynFinLen();
        sendRstAck(0, ackNo, destAddr, srcAddr, tcpseg->getDestPort(), tcpseg->getSrcPort());
    }
    else {
        EV_DETAIL << "ACK bit set: sending RST\n";
        sendRst(tcpseg->getAckNo(), destAddr, srcAddr, tcpseg->getDestPort(), tcpseg->getSrcPort());
    }
}

void TcpConnection::store_connection_info(const Ptr<const TcpHeader>& tcpseg, L3Address src, L3Address dest) {
    //v2 marking and ordering
    if (!connection_information_stored) {
        EV << "Storing connection information!" << endl;
        connection_information_stored = true;
        source_ip = dest.str(); // source is the connection itself which is the dest of this packet
        source_port = std::to_string(tcpseg->getDestinationPort());
        destination_ip = src.str();
        destination_port = std::to_string(tcpseg->getSourcePort());
        EV << source_ip << endl;
        EV << source_port << endl;
        EV << destination_ip << endl;
        EV << destination_port << endl;
        EV << "--------------------------------------------" << endl;
    }
}

TcpEventCode TcpConnection::process_RCV_SEGMENT(Packet *packet, const Ptr<const TcpHeader>& tcpseg, L3Address src, L3Address dest)
{
    EV_INFO << "Seg arrived: ";
    printSegmentBrief(packet, tcpseg);
    EV_DETAIL << "TCB: " << state->str() << "\n";

    uint32_t srcAddr = 0;
    uint32_t destAddr = 0;

    // Se gli indirizzi sono IPv4, convertili
    if (src.getType() == L3Address::IPv4) {
        srcAddr = src.toIpv4().getInt();
    }
    if (dest.getType() == L3Address::IPv4) {
        destAddr = dest.toIpv4().getInt();
    }
    
    FlowKey f1{ tcpseg->getSequenceNo(), 
                static_cast<uint16_t>(tcpseg->getSrcPort()), 
                static_cast<uint16_t>(tcpseg->getDestPort()), 
                srcAddr, 
                destAddr };

    if (packet->getByteLength() > B(tcpseg->getChunkLength()).get())
        emit(rcvSeqSignal, f1.getSafeHash());

    //emit(rcvSeqSignal, tcpseg->getSequenceNo());
    emit(rcvAckSignal, tcpseg->getAckNo());

    //v2 marking and ordering
    store_connection_info(tcpseg, src, dest);

    //
    // Note: this code is organized exactly as RFC 793, section "3.9 Event
    // Processing", subsection "SEGMENT ARRIVES".
    //
    TcpEventCode event;

    if (fsm.getState() == TCP_S_LISTEN) {
        event = processSegmentInListen(packet, tcpseg, src, dest);
    }
    else if (fsm.getState() == TCP_S_SYN_SENT) {
        event = processSegmentInSynSent(packet, tcpseg, src, dest);
    }
    else {
        // RFC 793 steps "first check sequence number", "second check the RST bit", etc
        event = processSegment1stThru8th(packet, tcpseg, src, dest);
    }

    delete packet;
    return event;
}

bool TcpConnection::hasEnoughSpaceForSegmentInReceiveQueue(Packet *packet, const Ptr<const TcpHeader>& tcpseg)
{
    //TODO must rewrite it
    //return (state->freeRcvBuffer >= tcpseg->getPayloadLength()); // enough freeRcvBuffer in rcvQueue for new segment?

    long int payloadLength = packet->getByteLength() - B(tcpseg->getHeaderLength()).get();
    uint32_t payloadSeq = tcpseg->getSequenceNo();
    uint32 firstSeq = receiveQueue->getFirstSeqNo();
    if (seqLess(payloadSeq, firstSeq)) {
        long delta = firstSeq - payloadSeq;
        payloadSeq += delta;
        payloadLength -= delta;
    }
    return seqLE(firstSeq, payloadSeq) && seqLE(payloadSeq + payloadLength, firstSeq + state->maxRcvBuffer);
}

TcpEventCode TcpConnection::processSegment1stThru8th(Packet *packet, const Ptr<const TcpHeader>& tcpseg, L3Address src, L3Address dest)
{
    //
    // RFC 793: first check sequence number
    //

    EV << "SEPEHR: processSegment1stThru8th called" << endl;

    bool acceptable = true;

    std::string packet_name = packet->getName();
    if (packet_name.find("tcpseg") != std::string::npos && tcpseg->getBolt_inc()) {
        state->last_data_packet_inc = true;
    } else {
        state->last_data_packet_inc = false;
    }
    if (packet_name.find("SRC") != std::string::npos) {
        if (!tcpseg->getSrcbit())
            throw cRuntimeError("!tcpseg->getSrcbit()");
//        if (fsm.getState() != TCP_S_ESTABLISHED)
//            return TCP_E_IGNORE;
        tcpAlgorithm->receivedSrc(packet, tcpseg);
        return TCP_E_IGNORE;
    }

    if (tcpseg->getHeaderLength() > TCP_MIN_HEADER_LENGTH) {    // Header options present? TCP_HEADER_OCTETS = 20
        // PAWS
        if (state->ts_enabled) {
            simtime_t tsval = getTSval(tcpseg);
            if (tsval != 0 && (tsval < state->ts_recent) &&
                (simTime() - state->time_last_data_sent) > PAWS_IDLE_TIME_THRESH)    // PAWS_IDLE_TIME_THRESH = 24 days
            {
                EV_DETAIL << "PAWS: Segment is not acceptable, TSval=" << tsval << " in "
                          << stateName(fsm.getState()) << " state received: dropping segment\n";
                acceptable = false;
            }
        }

        readHeaderOptions(tcpseg);
    }

    if (acceptable)
        acceptable = isSegmentAcceptable(packet, tcpseg);

    int payloadLength = packet->getByteLength() - B(tcpseg->getHeaderLength()).get();

    if (!acceptable) {
        //"
        // If an incoming segment is not acceptable, an acknowledgment
        // should be sent in reply (unless the RST bit is set, if so drop
        // the segment and return):
        //
        //  <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>
        //"
        if (tcpseg->getRstBit()) {
            EV_DETAIL << "RST with unacceptable seqNum: dropping\n";
        }
        else {
            if (tcpseg->getSynBit()) {
                EV_DETAIL << "SYN with unacceptable seqNum in " << stateName(fsm.getState()) << " state received (SYN duplicat?)\n";
            }
            else if (payloadLength > 0 && state->sack_enabled && seqLess((tcpseg->getSequenceNo() + payloadLength), state->rcv_nxt)) {
                state->start_seqno = tcpseg->getSequenceNo();
                state->end_seqno = tcpseg->getSequenceNo() + payloadLength;
                state->snd_dsack = true;
                EV_DETAIL << "SND_D-SACK SET (dupseg rcvd)\n";
            }

            EV_DETAIL << "Segment seqNum not acceptable, sending ACK with current receive seq\n";
            // RFC 2018, page 4:
            // "The receiver SHOULD send an ACK for every valid segment that arrives
            // containing new data, and each of these "duplicate" ACKs SHOULD bear a
            // SACK option."
            //
            // The received segment is not "valid" therefore the ACK will not bear a SACK option, if snd_dsack (D-SACK) is not set.

            // swift: this actually does not need to echo any time
            sendAck();
        }

        state->rcv_naseg++;

        emit(rcvNASegSignal, state->rcv_naseg);

        return TCP_E_IGNORE;
    }

    // ECN
    //Sougol: No need to change the state here, because we already have left the sate
    if (tcpseg->getCwrBit() == true) {
        EV_INFO << "Received CWR... Leaving ecnEcho State\n";
        state->ecnEchoState = false;
    }

    //
    // RFC 793: second check the RST bit,
    //
    if (tcpseg->getRstBit()) {
        // Note: if we come from LISTEN, processSegmentInListen() has already handled RST.
        switch (fsm.getState()) {
            case TCP_S_SYN_RCVD:
                //"
                // If this connection was initiated with a passive OPEN (i.e.,
                // came from the LISTEN state), then return this connection to
                // LISTEN state and return.  The user need not be informed.  If
                // this connection was initiated with an active OPEN (i.e., came
                // from SYN-SENT state) then the connection was refused, signal
                // the user "connection refused".  In either case, all segments
                // on the retransmission queue should be removed.  And in the
                // active OPEN case, enter the CLOSED state and delete the TCB,
                // and return.
                //"
                return processRstInSynReceived(tcpseg);

            case TCP_S_ESTABLISHED:
            case TCP_S_FIN_WAIT_1:
            case TCP_S_FIN_WAIT_2:
            case TCP_S_CLOSE_WAIT:
                //"
                // If the RST bit is set then, any outstanding RECEIVEs and SEND
                // should receive "reset" responses.  All segment queues should be
                // flushed.  Users should also receive an unsolicited general
                // "connection reset" signal.
                //
                // Enter the CLOSED state, delete the TCB, and return.
                //"
                EV_DETAIL << "RST: performing connection reset, closing connection\n";
                sendIndicationToApp(TCP_I_CONNECTION_RESET);
                return TCP_E_RCV_RST;    // this will trigger state transition

            case TCP_S_CLOSING:
            case TCP_S_LAST_ACK:
            case TCP_S_TIME_WAIT:
                //"
                // enter the CLOSED state, delete the TCB, and return.
                //"
                EV_DETAIL << "RST: closing connection\n";
                return TCP_E_RCV_RST;    // this will trigger state transition

            default:
                ASSERT(0);
                break;
        }
    }

    // RFC 793: third check security and precedence
    // This step is ignored.

    //
    // RFC 793: fourth, check the SYN bit,
    //
    if (tcpseg->getSynBit()) {
        //"
        // If the SYN is in the window it is an error, send a reset, any
        // outstanding RECEIVEs and SEND should receive "reset" responses,
        // all segment queues should be flushed, the user should also
        // receive an unsolicited general "connection reset" signal, enter
        // the CLOSED state, delete the TCB, and return.
        //
        // If the SYN is not in the window this step would not be reached
        // and an ack would have been sent in the first step (sequence
        // number check).
        //"

        ASSERT(isSegmentAcceptable(packet, tcpseg));    // assert SYN is in the window
        EV_DETAIL << "SYN is in the window: performing connection reset, closing connection\n";
        sendIndicationToApp(TCP_I_CONNECTION_RESET);
        return TCP_E_RCV_UNEXP_SYN;
    }

    //
    // RFC 793: fifth check the ACK field,
    //
    if (!tcpseg->getAckBit()) {
        // if the ACK bit is off drop the segment and return
        EV_INFO << "ACK not set, dropping segment\n";
        return TCP_E_IGNORE;
    }

    uint32 old_snd_una = state->snd_una;

    TcpEventCode event = TCP_E_IGNORE;

    if (fsm.getState() == TCP_S_SYN_RCVD) {
        //"
        // If SND.UNA =< SEG.ACK =< SND.NXT then enter ESTABLISHED state
        // and continue processing.
        //
        // If the segment acknowledgment is not acceptable, form a
        // reset segment,
        //
        //  <SEQ=SEG.ACK><CTL=RST>
        //
        // and send it.
        //"
        if (!seqLE(state->snd_una, tcpseg->getAckNo()) || !seqLE(tcpseg->getAckNo(), state->snd_nxt)) {
            sendRst(tcpseg->getAckNo());
            return TCP_E_IGNORE;
        }

        // notify tcpAlgorithm and app layer
        tcpAlgorithm->established(false);

        if (isToBeAccepted())
            sendAvailableIndicationToApp();
        else
            sendEstabIndicationToApp();

        // This will trigger transition to ESTABLISHED. Timers and notifying
        // app will be taken care of in stateEntered().
        event = TCP_E_RCV_ACK;
    }

    uint32 old_snd_nxt = state->snd_nxt;    // later we'll need to see if snd_nxt changed
    // Note: If one of the last data segments is lost while already in LAST-ACK state (e.g. if using TCPEchoApps)
    // TCP must be able to process acceptable acknowledgments, however please note RFC 793, page 73:
    // "LAST-ACK STATE
    //    The only thing that can arrive in this state is an
    //    acknowledgment of our FIN.  If our FIN is now acknowledged,
    //    delete the TCB, enter the CLOSED state, and return."
    if (fsm.getState() == TCP_S_SYN_RCVD || fsm.getState() == TCP_S_ESTABLISHED ||
        fsm.getState() == TCP_S_FIN_WAIT_1 || fsm.getState() == TCP_S_FIN_WAIT_2 ||
        fsm.getState() == TCP_S_CLOSE_WAIT || fsm.getState() == TCP_S_CLOSING ||
        fsm.getState() == TCP_S_LAST_ACK)
    {
        //
        // ESTABLISHED processing:
        //"
        //  If SND.UNA < SEG.ACK =< SND.NXT then, set SND.UNA <- SEG.ACK.
        //  Any segments on the retransmission queue which are thereby
        //  entirely acknowledged are removed.  Users should receive
        //  positive acknowledgments for buffers which have been SENT and
        //  fully acknowledged (i.e., SEND buffer should be returned with
        //  "ok" response).  If the ACK is a duplicate
        //  (SEG.ACK < SND.UNA), it can be ignored.  If the ACK acks
        //  something not yet sent (SEG.ACK > SND.NXT) then send an ACK,
        //  drop the segment, and return.
        //
        //  If SND.UNA < SEG.ACK =< SND.NXT, the send window should be
        //  updated.  If (SND.WL1 < SEG.SEQ or (SND.WL1 = SEG.SEQ and
        //  SND.WL2 =< SEG.ACK)), set SND.WND <- SEG.WND, set
        //  SND.WL1 <- SEG.SEQ, and set SND.WL2 <- SEG.ACK.
        //
        //  Note that SND.WND is an offset from SND.UNA, that SND.WL1
        //  records the sequence number of the last segment used to update
        //  SND.WND, and that SND.WL2 records the acknowledgment number of
        //  the last segment used to update SND.WND.  The check here
        //  prevents using old segments to update the window.
        //"
        bool ok = processAckInEstabEtc(packet, tcpseg);

        if (!ok) {
            EV << "SEPEHR: ACK acks something not yet sent, drop it" << endl;
            return TCP_E_IGNORE; // if acks something not yet sent, drop it
        }

    }

    if ((fsm.getState() == TCP_S_FIN_WAIT_1 && state->fin_ack_rcvd)) {
        //"
        // FIN-WAIT-1 STATE
        //   In addition to the processing for the ESTABLISHED state, if
        //   our FIN is now acknowledged then enter FIN-WAIT-2 and continue
        //   processing in that state.
        //"
        event = TCP_E_RCV_ACK;    // will trigger transition to FIN-WAIT-2
    }

    if (fsm.getState() == TCP_S_FIN_WAIT_2) {
        //"
        // FIN-WAIT-2 STATE
        //  In addition to the processing for the ESTABLISHED state, if
        //  the retransmission queue is empty, the user's CLOSE can be
        //  acknowledged ("ok") but do not delete the TCB.
        //"
        // nothing to do here (in our model, used commands don't need to be
        // acknowledged)
    }

    if (fsm.getState() == TCP_S_CLOSING) {
        //"
        // In addition to the processing for the ESTABLISHED state, if
        // the ACK acknowledges our FIN then enter the TIME-WAIT state,
        // otherwise ignore the segment.
        //"
        if (state->fin_ack_rcvd) {
            EV_INFO << "Our FIN acked -- can go to TIME_WAIT now\n";
            event = TCP_E_RCV_ACK;    // will trigger transition to TIME-WAIT
            EV << "SEPEHR: Scheduling the2MSLTimer(1) to " << (2*tcpMain->getMsl()) << endl;
            scheduleTimeout(the2MSLTimer, 2*tcpMain->getMsl());    // start timer

            // we're entering TIME_WAIT, so we can signal CLOSED the user
            // (the only thing left to do is wait until the 2MSL timer expires)
        }
    }

    if (fsm.getState() == TCP_S_LAST_ACK) {
        //"
        // The only thing that can arrive in this state is an
        // acknowledgment of our FIN.  If our FIN is now acknowledged,
        // delete the TCB, enter the CLOSED state, and return.
        //"
        if (state->send_fin && tcpseg->getAckNo() == state->snd_fin_seq + 1) {
            EV_INFO << "Last ACK arrived\n";
            return TCP_E_RCV_ACK;    // will trigger transition to CLOSED
        }
    }

    if (fsm.getState() == TCP_S_TIME_WAIT) {
        //"
        // The only thing that can arrive in this state is a
        // retransmission of the remote FIN.  Acknowledge it, and restart
        // the 2 MSL timeout.
        //"
        // And we are staying in the TIME_WAIT state.
        //

        // swift: this actually does not need to echo any time
        sendAck();
        cancelEvent(the2MSLTimer);
        EV << "SEPEHR: Scheduling the2MSLTimer(2) to " << (2*tcpMain->getMsl()) << endl;
        scheduleTimeout(the2MSLTimer, 2*tcpMain->getMsl());
    }

    //
    // RFC 793: sixth, check the URG bit,
    //
    if (tcpseg->getUrgBit() && (fsm.getState() == TCP_S_ESTABLISHED ||
                                fsm.getState() == TCP_S_FIN_WAIT_1 || fsm.getState() == TCP_S_FIN_WAIT_2))
    {
        //"
        // If the URG bit is set, RCV.UP <- max(RCV.UP,SEG.UP), and signal
        // the user that the remote side has urgent data if the urgent
        // pointer (RCV.UP) is in advance of the data consumed.  If the
        // user has already been signaled (or is still in the "urgent
        // mode") for this continuous sequence of urgent data, do not
        // signal the user again.
        //"

        // TBD: URG currently not supported
    }

    //
    // RFC 793: seventh, process the segment text,
    //
    uint32 old_rcv_nxt = state->rcv_nxt;    // if rcv_nxt changes, we need to send/schedule an ACK

    if (fsm.getState() == TCP_S_SYN_RCVD || fsm.getState() == TCP_S_ESTABLISHED ||
        fsm.getState() == TCP_S_FIN_WAIT_1 || fsm.getState() == TCP_S_FIN_WAIT_2)
    {
        //"
        // Once in the ESTABLISHED state, it is possible to deliver segment
        // text to user RECEIVE buffers.  Text from segments can be moved
        // into buffers until either the buffer is full or the segment is
        // empty.  If the segment empties and carries an PUSH flag, then
        // the user is informed, when the buffer is returned, that a PUSH
        // has been received.
        //
        // When the TCP takes responsibility for delivering the data to the
        // user it must also acknowledge the receipt of the data.
        //
        // Once the TCP takes responsibility for the data it advances
        // RCV.NXT over the data accepted, and adjusts RCV.WND as
        // appropriate to the current buffer availability.  The total of
        // RCV.NXT and RCV.WND should not be reduced.
        //
        // Please note the window management suggestions in section 3.7.
        //
        // Send an acknowledgment of the form:
        //
        //   <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>
        //
        // This acknowledgment should be piggybacked on a segment being
        // transmitted if possible without incurring undue delay.
        //"

        if (payloadLength > 0) {
//            if (auto ecnTag = packet->findTag<EcnInd>()) {
//                if (ecnTag->getExplicitCongestionNotification() == IP_ECN_CE)
//                    state->ecnCe = true;
//            }

            // check for full sized segment
            if ((uint32_t)payloadLength == state->snd_mss || (uint32_t)payloadLength + B(tcpseg->getHeaderLength() - TCP_MIN_HEADER_LENGTH).get() == state->snd_mss)
                state->full_sized_segment_counter++;

            // check for persist probe
            if (payloadLength == 1)
                state->ack_now = true; // TODO how to check if it is really a persist probe?

            updateRcvQueueVars();

            if (hasEnoughSpaceForSegmentInReceiveQueue(packet, tcpseg)) {    // enough freeRcvBuffer in rcvQueue for new segment?
                EV_DETAIL << "Processing segment text in a data transfer state\n";

                // insert into receive buffers. If this segment is contiguous with
                // previously received ones (seqNo == rcv_nxt), rcv_nxt can be increased;
                // otherwise it stays the same but the data must be cached nevertheless
                // (to avoid "Failure to retain above-sequence data" problem, RFC 2525
                // section 2.5).

                uint32 old_usedRcvBuffer = state->usedRcvBuffer;
                state->rcv_nxt = receiveQueue->insertBytesFromSegment(packet, tcpseg);

                if (seqGreater(state->snd_una, old_snd_una)) {
                    state->last_ack_packet_rcvd = packet->dup();
                    // notify
                    tcpAlgorithm->receivedDataAck(old_snd_una);
                    delete state->last_ack_packet_rcvd;

                    // in the receivedDataAck we need the old value
                    state->dupacks = 0;

//                    emit(dupAcksSignal, state->dupacks);
                }


                // out-of-order segment and that's why the segment that we were waiting for before
                // receiving the packet is equal to the segment that we are waiting for after receiving it
                if (old_rcv_nxt == state->rcv_nxt) {
                    state->rcv_oooseg++;
//                    std::cout << "oooseg received" << endl;

                    uint32_t srcAddr = 0;
                    uint32_t destAddr = 0;

                    // Se gli indirizzi sono IPv4, convertili
                    if (src.getType() == L3Address::IPv4) {
                        srcAddr = src.toIpv4().getInt();
                    }
                    if (dest.getType() == L3Address::IPv4) {
                        destAddr = dest.toIpv4().getInt();
                    }

                    FlowKey f1{ tcpseg->getSequenceNo(), 
                                static_cast<uint16_t>(tcpseg->getSrcPort()), 
                                static_cast<uint16_t>(tcpseg->getDestPort()), 
                                srcAddr, 
                                destAddr };

                    emit(rcvOooSegSignal, f1.getSafeHash());
                    tcpMain->num_received_ooo_segs++;

                    // RFC 2018, page 4:
                    // "The receiver SHOULD send an ACK for every valid segment that arrives
                    // containing new data, and each of these "duplicate" ACKs SHOULD bear a
                    // SACK option."
                    if (state->sack_enabled) {
                        // store start and end sequence numbers of current oooseg in state variables
                        state->start_seqno = tcpseg->getSequenceNo();
                        state->end_seqno = tcpseg->getSequenceNo() + payloadLength;

                        if (old_usedRcvBuffer == receiveQueue->getAmountOfBufferedBytes()) {    // D-SACK
                            state->snd_dsack = true;
                            EV_DETAIL << "SND_D-SACK SET (old_rcv_nxt == rcv_nxt duplicated oooseg rcvd)\n";
                        }
                        else {    // SACK
                            state->snd_sack = true;
                            EV_DETAIL << "SND_SACK SET (old_rcv_nxt == rcv_nxt oooseg rcvd)\n";
                        }
                    }

                    tcpAlgorithm->receivedOutOfOrderSegment();
                }
                else {
                    // forward data to app
                    //
                    // FIXME observe PSH bit
                    //
                    // FIXME we should implement socket READ command, and pass up only
                    // as many bytes as requested. rcv_wnd should be decreased
                    // accordingly!
                    //
                    if (!isToBeAccepted())
                        sendAvailableDataToApp();

                    // if this segment "filled the gap" until the previously arrived segment
                    // that carried a FIN (i.e.rcv_nxt == rcv_fin_seq), we have to advance
                    // rcv_nxt over the FIN.
                    if (state->fin_rcvd && state->rcv_nxt == state->rcv_fin_seq) {
                        state->ack_now = true;    // although not mentioned in [Stevens, W.R.: TCP/IP Illustrated, Volume 2, page 861] seems like we have to set ack_now
                        EV_DETAIL << "All segments arrived up to the FIN segment, advancing rcv_nxt over the FIN\n";
                        state->rcv_nxt = state->rcv_fin_seq + 1;
                        // state transitions will be done in the state machine, here we just set
                        // the proper event code (TCP_E_RCV_FIN or TCP_E_RCV_FIN_ACK)
                        event = TCP_E_RCV_FIN;

                        switch (fsm.getState()) {
                            case TCP_S_FIN_WAIT_1:
                                if (state->fin_ack_rcvd) {
                                    event = TCP_E_RCV_FIN_ACK;
                                    // start the time-wait timer, turn off the other timers
                                    cancelEvent(finWait2Timer);
                                    EV << "SEPEHR: Scheduling the2MSLTimer(3) to " << (2*tcpMain->getMsl()) << endl;
                                    scheduleTimeout(the2MSLTimer, 2*tcpMain->getMsl());

                                    // we're entering TIME_WAIT, so we can signal CLOSED the user
                                    // (the only thing left to do is wait until the 2MSL timer expires)
                                }
                                break;

                            case TCP_S_FIN_WAIT_2:
                                // Start the time-wait timer, turn off the other timers.
                                cancelEvent(finWait2Timer);
                                EV << "SEPEHR: Scheduling the2MSLTimer(4) to " << (2*tcpMain->getMsl()) << endl;
                                scheduleTimeout(the2MSLTimer, 2*tcpMain->getMsl());

                                // we're entering TIME_WAIT, so we can signal CLOSED the user
                                // (the only thing left to do is wait until the 2MSL timer expires)
                                break;

                            case TCP_S_TIME_WAIT:
                                // Restart the 2 MSL time-wait timeout.
                                cancelEvent(the2MSLTimer);
                                EV << "SEPEHR: Scheduling the2MSLTimer(5) to " << (2*tcpMain->getMsl()) << endl;
                                scheduleTimeout(the2MSLTimer, 2*tcpMain->getMsl());
                                break;

                            default:
                                break;
                        }
                    }
                }
            }
            else {    // not enough freeRcvBuffer in rcvQueue for new segment
                state->tcpRcvQueueDrops++;    // update current number of tcp receive queue drops

                emit(tcpRcvQueueDropsSignal, state->tcpRcvQueueDrops);

                // if the ACK bit is off drop the segment and return
                EV_WARN << "RcvQueueBuffer has run out, dropping segment\n";
                return TCP_E_IGNORE;
            }
        }
    }

    //
    // RFC 793: eighth, check the FIN bit,
    //
    if (tcpseg->getFinBit()) {
        state->ack_now = true;

        //"
        // If the FIN bit is set, signal the user "connection closing" and
        // return any pending RECEIVEs with same message, advance RCV.NXT
        // over the FIN, and send an acknowledgment for the FIN.  Note that
        // FIN implies PUSH for any segment text not yet delivered to the
        // user.
        //"

        // Note: seems like RFC 793 is not entirely correct here: if the
        // segment is "above sequence" (ie. RCV.NXT < SEG.SEQ), we cannot
        // advance RCV.NXT over the FIN. Instead we remember this sequence
        // number and do it later.
        uint32 fin_seq = (uint32)tcpseg->getSequenceNo() + (uint32)payloadLength;

        if (state->rcv_nxt == fin_seq) {
            // advance rcv_nxt over FIN now
            EV_INFO << "FIN arrived, advancing rcv_nxt over the FIN\n";
            state->rcv_nxt++;
            // state transitions will be done in the state machine, here we just set
            // the proper event code (TCP_E_RCV_FIN or TCP_E_RCV_FIN_ACK)
            event = TCP_E_RCV_FIN;

            switch (fsm.getState()) {
                case TCP_S_FIN_WAIT_1:
                    if (state->fin_ack_rcvd) {
                        event = TCP_E_RCV_FIN_ACK;
                        // start the time-wait timer, turn off the other timers
                        cancelEvent(finWait2Timer);
                        EV << "SEPEHR: Scheduling the2MSLTimer(6) to " << (2*tcpMain->getMsl()) << endl;
                        scheduleTimeout(the2MSLTimer, 2*tcpMain->getMsl());

                        // we're entering TIME_WAIT, so we can signal CLOSED the user
                        // (the only thing left to do is wait until the 2MSL timer expires)
                    }
                    break;

                case TCP_S_FIN_WAIT_2:
                    // Start the time-wait timer, turn off the other timers.
                    cancelEvent(finWait2Timer);
                    EV << "SEPEHR: Should normally schedule the2MSLTimer(7) to " << (2*tcpMain->getMsl()) <<
                            "but we're scheduling it to " << simTime() + 0.000001 << endl;
                    scheduleTimeout(the2MSLTimer, 2*tcpMain->getMsl());

                    // we're entering TIME_WAIT, so we can signal CLOSED the user
                    // (the only thing left to do is wait until the 2MSL timer expires)
                    break;

                case TCP_S_TIME_WAIT:
                    // Restart the 2 MSL time-wait timeout.
                    cancelEvent(the2MSLTimer);
                    EV << "SEPEHR: Scheduling the2MSLTimer(8) to " << (2*tcpMain->getMsl()) << endl;
                    scheduleTimeout(the2MSLTimer, 2*tcpMain->getMsl());
                    break;

                default:
                    break;
            }
        }
        else {
            // we'll have to do it later (when an arriving segment "fills the gap")
            EV_DETAIL << "FIN segment above sequence, storing sequence number of FIN\n";
            state->fin_rcvd = true;
            state->rcv_fin_seq = fin_seq;
        }

        // TBD do PUSH stuff
    }

    if (old_rcv_nxt != state->rcv_nxt) {
        // if rcv_nxt changed, either because we received segment text or we
        // received a FIN that needs to be acked (or both), we need to send or
        // schedule an ACK.
        if (state->sack_enabled) {
            if (receiveQueue->getQueueLength() != 0) {
                // RFC 2018, page 4:
                // "If sent at all, SACK options SHOULD be included in all ACKs which do
                // not ACK the highest sequence number in the data receiver's queue."
                state->start_seqno = tcpseg->getSequenceNo();
                state->end_seqno = tcpseg->getSequenceNo() + payloadLength;
                state->snd_sack = true;
                EV_DETAIL << "SND_SACK SET (rcv_nxt changed, but receiveQ is not empty)\n";
                state->ack_now = true;    // although not mentioned in [Stevens, W.R.: TCP/IP Illustrated, Volume 2, page 861] seems like we have to set ack_now
            }
        }

        // tcpAlgorithm decides when and how to do ACKs
        tcpAlgorithm->receiveSeqChanged();
//        if (!tcpseg->getWait_for_whole_pack_from_gro()) {
//            tcpAlgorithm->receiveSeqChanged();
//        }
//        else
//            std::cout << "Wait signal is set from gro. Waiting..." << endl;
    }

    if ((fsm.getState() == TCP_S_ESTABLISHED || fsm.getState() == TCP_S_SYN_RCVD) &&
        state->send_fin && state->snd_nxt == state->snd_fin_seq + 1)
    {
        // if the user issued the CLOSE command a long time ago and we've just
        // managed to send off FIN, we simulate a CLOSE command now (we had to
        // defer it at that time because we still had data in the send queue.)
        // This CLOSE will take us into the FIN_WAIT_1 state.
        EV_DETAIL << "Now we can do the CLOSE which was deferred a while ago\n";
        event = TCP_E_CLOSE;
    }

    if (fsm.getState() == TCP_S_CLOSE_WAIT && state->send_fin &&
        state->snd_nxt == state->snd_fin_seq + 1 && old_snd_nxt != state->snd_nxt)
    {
        // if we're in CLOSE_WAIT and we just got to sent our long-pending FIN,
        // we simulate a CLOSE command now (we had to defer it at that time because
        // we still had data in the send queue.) This CLOSE will take us into the
        // LAST_ACK state.
        EV_DETAIL << "Now we can do the CLOSE which was deferred a while ago\n";
        event = TCP_E_CLOSE;
    }

    return event;
}

//----

TcpEventCode TcpConnection::processSegmentInListen(Packet *packet, const Ptr<const TcpHeader>& tcpseg, L3Address srcAddr, L3Address destAddr)
{
    EV_DETAIL << "Processing segment in LISTEN\n";

    //"
    // first check for an RST
    //   An incoming RST should be ignored.  Return.
    //"
    if (tcpseg->getRstBit()) {
        EV_INFO << "RST bit set: dropping segment\n";
        return TCP_E_IGNORE;
    }

    //"
    // second check for an ACK
    //    Any acknowledgment is bad if it arrives on a connection still in
    //    the LISTEN state.  An acceptable reset segment should be formed
    //    for any arriving ACK-bearing segment.  The RST should be
    //    formatted as follows:
    //
    //      <SEQ=SEG.ACK><CTL=RST>
    //
    //    Return.
    //"
    if (tcpseg->getAckBit()) {
        EV_INFO << "ACK bit set: dropping segment and sending RST\n";
        sendRst(tcpseg->getAckNo(), destAddr, srcAddr, tcpseg->getDestPort(), tcpseg->getSrcPort());
        return TCP_E_IGNORE;
    }

    //"
    // third check for a SYN
    //"
    if (tcpseg->getSynBit()) {
        if (tcpseg->getFinBit()) {
            // Looks like implementations vary on how to react to SYN+FIN.
            // Some treat it as plain SYN (and reply with SYN+ACK), some send RST+ACK.
            // Let's just do the former here.
            EV_INFO << "SYN+FIN received: ignoring FIN\n";
        }

        EV_DETAIL << "SYN bit set: filling in foreign socket and sending SYN+ACK\n";
        tcpMain->num_connections++;

        //"
        // If the listen was not fully specified (i.e., the foreign socket was not
        // fully specified), then the unspecified fields should be filled in now.
        //"
        //
        // Also, we may need to fork, in order to leave another connection
        // LISTENing on the port. Note: forking will change our socketId.
        //
        if (state->fork) {
            // TODO maybe change the code: current connection stay listening and created connection will be used for communication???

            TcpConnection *conn = cloneListeningConnection();    // "conn" is the clone which will stay LISTENing, while "this" gets updated with the remote address
            tcpMain->addForkedConnection(this, conn, destAddr, srcAddr, tcpseg->getDestPort(), tcpseg->getSrcPort());
            EV_DETAIL << "Connection forked: new connection got new socketId=" << conn->socketId << ", "
                                                                                           "old connection keeps LISTENing with socketId=" << socketId << "\n";
            TcpEventCode forkEvent = conn->processSynInListen(packet, tcpseg, srcAddr, destAddr);
            conn->performStateTransition(forkEvent);

            return TCP_E_IGNORE;
        }
        else {
            tcpMain->updateSockPair(this, destAddr, srcAddr, tcpseg->getDestPort(), tcpseg->getSrcPort());
            return processSynInListen(packet, tcpseg, srcAddr, destAddr);
        }
    }

    //"
    //  fourth other text or control
    //   So you are unlikely to get here, but if you do, drop the segment, and return.
    //"
    EV_WARN << "Unexpected segment: dropping it\n";
    return TCP_E_IGNORE;
}

TcpEventCode TcpConnection::processSynInListen(Packet *packet, const Ptr<const TcpHeader>& tcpseg, L3Address srcAddr, L3Address destAddr)
{
    //"
    //  Set RCV.NXT to SEG.SEQ+1, IRS is set to SEG.SEQ and any other
    //  control or text should be queued for processing later.  ISS
    //  should be selected and a SYN segment sent of the form:
    //
    //    <SEQ=ISS><ACK=RCV.NXT><CTL=SYN,ACK>
    //
    //  SND.NXT is set to ISS+1 and SND.UNA to ISS.  The connection
    //  state should be changed to SYN-RECEIVED.
    //"
    state->rcv_nxt = tcpseg->getSequenceNo() + 1;
    state->rcv_adv = state->rcv_nxt + state->rcv_wnd;

    emit(rcvAdvSignal, state->rcv_adv);

    state->irs = tcpseg->getSequenceNo();
    receiveQueue->init(state->rcv_nxt);    // FIXME may init twice...
    selectInitialSeqNum();

    // although not mentioned in RFC 793, seems like we have to pick up
    // initial snd_wnd from the segment here.
    updateWndInfo(tcpseg, true);

    if (tcpseg->getHeaderLength() > TCP_MIN_HEADER_LENGTH) // Header options present?
        readHeaderOptions(tcpseg);

//    if (state->ecnEnabled && (tcpseg->getEceBit() && tcpseg->getCwrBit())) {
//        state->ecnSetupSynReceived = true;
//        state->ecnActive = true;
//    }

    state->ack_now = true;

    // ECN
    if (tcpseg->getEceBit() == true && tcpseg->getCwrBit() == true) {
        state->endPointIsWillingECN = true;
        EV << "ECN-setup SYN packet received\n";
    }

    sendSynAck();
    startSynRexmitTimer();

    if (!connEstabTimer->isScheduled())
        scheduleTimeout(connEstabTimer, TCP_TIMEOUT_CONN_ESTAB);

    //"
    // Note that any other incoming control or data (combined with SYN)
    // will be processed in the SYN-RECEIVED state, but processing of SYN
    // and ACK should not be repeated.
    //"
    // We don't send text in SYN or SYN+ACK, but accept it. Otherwise
    // there isn't much left to do: RST, SYN, ACK, FIN got processed already,
    // so there's only URG and PSH left to handle.
    //
    if (B(packet->getByteLength()) > tcpseg->getHeaderLength()) {
        updateRcvQueueVars();

        if (hasEnoughSpaceForSegmentInReceiveQueue(packet, tcpseg)) {    // enough freeRcvBuffer in rcvQueue for new segment?
            receiveQueue->insertBytesFromSegment(packet, tcpseg);
        }
        else {    // not enough freeRcvBuffer in rcvQueue for new segment
            state->tcpRcvQueueDrops++;    // update current number of tcp receive queue drops

            emit(tcpRcvQueueDropsSignal, state->tcpRcvQueueDrops);

            EV_WARN << "RcvQueueBuffer has run out, dropping segment\n";
            return TCP_E_IGNORE;
        }
    }

    if (tcpseg->getUrgBit() || tcpseg->getPshBit())
        EV_DETAIL << "Ignoring URG and PSH bits in SYN\n"; // TBD

    return TCP_E_RCV_SYN;    // this will take us to SYN_RCVD
}

TcpEventCode TcpConnection::processSegmentInSynSent(Packet *packet, const Ptr<const TcpHeader>& tcpseg, L3Address srcAddr, L3Address destAddr)
{
    EV_DETAIL << "Processing segment in SYN_SENT\n";

    //"
    // first check the ACK bit
    //
    //   If the ACK bit is set
    //
    //     If SEG.ACK =< ISS, or SEG.ACK > SND.NXT, send a reset (unless
    //     the RST bit is set, if so drop the segment and return)
    //
    //       <SEQ=SEG.ACK><CTL=RST>
    //
    //     and discard the segment.  Return.
    //
    //     If SND.UNA =< SEG.ACK =< SND.NXT then the ACK is acceptable.
    //"
    if (tcpseg->getAckBit()) {
        if (seqLE(tcpseg->getAckNo(), state->iss) || seqGreater(tcpseg->getAckNo(), state->snd_nxt)) {
            if (tcpseg->getRstBit())
                EV_DETAIL << "ACK+RST bit set but wrong AckNo, ignored\n";
            else {
                EV_DETAIL << "ACK bit set but wrong AckNo, sending RST\n";
                sendRst(tcpseg->getAckNo(), destAddr, srcAddr, tcpseg->getDestPort(), tcpseg->getSrcPort());
            }
            return TCP_E_IGNORE;
        }

        EV_DETAIL << "ACK bit set, AckNo acceptable\n";
    }

    //"
    // second check the RST bit
    //
    //   If the RST bit is set
    //
    //     If the ACK was acceptable then signal the user "error:
    //     connection reset", drop the segment, enter CLOSED state,
    //     delete TCB, and return.  Otherwise (no ACK) drop the segment
    //     and return.
    //"
    if (tcpseg->getRstBit()) {
        if (tcpseg->getAckBit()) {
            EV_DETAIL << "RST+ACK: performing connection reset\n";
            sendIndicationToApp(TCP_I_CONNECTION_RESET);

            return TCP_E_RCV_RST;
        }
        else {
            EV_DETAIL << "RST without ACK: dropping segment\n";

            return TCP_E_IGNORE;
        }
    }

    //"
    // third check the security and precedence -- not done
    //
    // fourth check the SYN bit
    //
    //   This step should be reached only if the ACK is ok, or there is
    //   no ACK, and it the segment did not contain a RST.
    //
    //   If the SYN bit is on and the security/compartment and precedence
    //   are acceptable then,
    //"
    if (tcpseg->getSynBit()) {
        //
        //   RCV.NXT is set to SEG.SEQ+1, IRS is set to
        //   SEG.SEQ.  SND.UNA should be advanced to equal SEG.ACK (if there
        //   is an ACK), and any segments on the retransmission queue which
        //   are thereby acknowledged should be removed.
        //
        state->rcv_nxt = tcpseg->getSequenceNo() + 1;
        state->rcv_adv = state->rcv_nxt + state->rcv_wnd;

        emit(rcvAdvSignal, state->rcv_adv);

        state->irs = tcpseg->getSequenceNo();
        receiveQueue->init(state->rcv_nxt);

        if (tcpseg->getAckBit()) {
            state->snd_una = tcpseg->getAckNo();
            sendQueue->discardUpTo(state->snd_una);

            if (state->sack_enabled)
                rexmitQueue->discardUpTo(state->snd_una);

            // although not mentioned in RFC 793, seems like we have to pick up
            // initial snd_wnd from the segment here.
            updateWndInfo(tcpseg, true);
        }

        // this also seems to be a good time to learn our local IP address
        // (was probably unspecified at connection open)
        tcpMain->updateSockPair(this, destAddr, srcAddr, tcpseg->getDestPort(), tcpseg->getSrcPort());

        //"
        //   If SND.UNA > ISS (our SYN has been ACKed), change the connection
        //   state to ESTABLISHED, form an ACK segment
        //
        //     <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>
        //
        //   and send it.  Data or controls which were queued for
        //   transmission may be included.  If there are other controls or
        //   text in the segment then continue processing at the sixth step
        //   below where the URG bit is checked, otherwise return.
        //"
        if (seqGreater(state->snd_una, state->iss)) {
            EV_INFO << "SYN+ACK bits set, connection established.\n";

            // RFC says "continue processing at the sixth step below where
            // the URG bit is checked". Those steps deal with: URG, segment text
            // (and PSH), and FIN.
            // Now: URG and PSH we don't support yet; in SYN+FIN we ignore FIN;
            // with segment text we just take it easy and put it in the receiveQueue
            // -- we'll forward it to the user when more data arrives.
            if (tcpseg->getFinBit())
                EV_DETAIL << "SYN+ACK+FIN received: ignoring FIN\n";

            if (B(packet->getByteLength()) > tcpseg->getHeaderLength()) {
                updateRcvQueueVars();

                if (hasEnoughSpaceForSegmentInReceiveQueue(packet, tcpseg)) {    // enough freeRcvBuffer in rcvQueue for new segment?
                    receiveQueue->insertBytesFromSegment(packet, tcpseg);    // TBD forward to app, etc.
                }
                else {    // not enough freeRcvBuffer in rcvQueue for new segment
                    state->tcpRcvQueueDrops++;    // update current number of tcp receive queue drops

                    emit(tcpRcvQueueDropsSignal, state->tcpRcvQueueDrops);

                    EV_WARN << "RcvQueueBuffer has run out, dropping segment\n";
                    return TCP_E_IGNORE;
                }
            }

            if (tcpseg->getUrgBit() || tcpseg->getPshBit())
                EV_DETAIL << "Ignoring URG and PSH bits in SYN+ACK\n"; // TBD

            if (tcpseg->getHeaderLength() > TCP_MIN_HEADER_LENGTH) // Header options present?
                readHeaderOptions(tcpseg);

//            //RFC 3168 - ECN enabled and received ECN-setup SYN-ACK packet
//            //ECN setup -> ECE = 1; CRW = 0
//            if (state->ecnEnabled && (tcpseg->getEceBit() && !tcpseg->getCwrBit())) {
//                state->ecnActive = true;
//            }

            // notify tcpAlgorithm (it has to send ACK of SYN) and app layer
            state->ack_now = true;
            tcpAlgorithm->established(true);
            tcpMain->emit(Tcp::tcpConnectionAddedSignal, this);
            sendEstabIndicationToApp();

            //ECN
            if (state->ecnSynSent) {
                if (tcpseg->getEceBit() && !tcpseg->getCwrBit()) {
                    state->ect = true;
                    EV << "ECN-setup SYN-ACK packet was received... ECN is enabled.\n";
                } else {
                    state->ect = false;
                    EV << "non-ECN-setup SYN-ACK packet was received... ECN is disabled.\n";
                }
                state->ecnSynSent = false;
            } else {
                state->ect = false;
                if (tcpseg->getEceBit() && !tcpseg->getCwrBit())
                    EV << "ECN-setup SYN-ACK packet was received... ECN is disabled.\n";
            }

            // This will trigger transition to ESTABLISHED. Timers and notifying
            // app will be taken care of in stateEntered().
            return TCP_E_RCV_SYN_ACK;
        }

        //"
        //   Otherwise enter SYN-RECEIVED, form a SYN,ACK segment
        //
        //     <SEQ=ISS><ACK=RCV.NXT><CTL=SYN,ACK>
        //
        //   and send it.  If there are other controls or text in the
        //   segment, queue them for processing after the ESTABLISHED state
        //   has been reached, return.
        //"
        EV_INFO << "SYN bit set: sending SYN+ACK\n";
        state->snd_max = state->snd_nxt = state->iss;
        sendSynAck();
        startSynRexmitTimer();

        // Note: code below is similar to processing SYN in LISTEN.

        // For consistency with that code, we ignore SYN+FIN here
        if (tcpseg->getFinBit())
            EV_DETAIL << "SYN+FIN received: ignoring FIN\n";

        // We don't send text in SYN or SYN+ACK, but accept it. Otherwise
        // there isn't much left to do: RST, SYN, ACK, FIN got processed already,
        // so there's only URG and PSH left to handle.
        if (B(packet->getByteLength()) > tcpseg->getHeaderLength()) {
            updateRcvQueueVars();

            if (hasEnoughSpaceForSegmentInReceiveQueue(packet, tcpseg)) {    // enough freeRcvBuffer in rcvQueue for new segment?
                receiveQueue->insertBytesFromSegment(packet, tcpseg);    // TBD forward to app, etc.
            }
            else {    // not enough freeRcvBuffer in rcvQueue for new segment
                state->tcpRcvQueueDrops++;    // update current number of tcp receive queue drops

                emit(tcpRcvQueueDropsSignal, state->tcpRcvQueueDrops);

                EV_WARN << "RcvQueueBuffer has run out, dropping segment\n";
                return TCP_E_IGNORE;
            }
        }

        if (tcpseg->getUrgBit() || tcpseg->getPshBit())
            EV_DETAIL << "Ignoring URG and PSH bits in SYN\n"; // TBD

        return TCP_E_RCV_SYN;
    }

    //"
    // fifth, if neither of the SYN or RST bits is set then drop the
    // segment and return.
    //"
    return TCP_E_IGNORE;
}

TcpEventCode TcpConnection::processRstInSynReceived(const Ptr<const TcpHeader>& tcpseg)
{
    EV_DETAIL << "Processing RST in SYN_RCVD\n";

    //"
    // If this connection was initiated with a passive OPEN (i.e.,
    // came from the LISTEN state), then return this connection to
    // LISTEN state and return.  The user need not be informed.  If
    // this connection was initiated with an active OPEN (i.e., came
    // from SYN-SENT state) then the connection was refused, signal
    // the user "connection refused".  In either case, all segments
    // on the retransmission queue should be removed.  And in the
    // active OPEN case, enter the CLOSED state and delete the TCB,
    // and return.
    //"

    sendQueue->discardUpTo(sendQueue->getBufferEndSeq());    // flush send queue

    if (state->sack_enabled)
        rexmitQueue->discardUpTo(rexmitQueue->getBufferEndSeq()); // flush rexmit queue

    if (state->active) {
        // signal "connection refused"
        sendIndicationToApp(TCP_I_CONNECTION_REFUSED);
    }

    // on RCV_RST, FSM will go either to LISTEN or to CLOSED, depending on state->active
    // FIXME if this was a forked connection, it should rather close than go back to listening (otherwise we'd now have two listening connections with the original one!)
    return TCP_E_RCV_RST;
}

bool TcpConnection::processAckInEstabEtc(Packet *packet, const Ptr<const TcpHeader>& tcpseg)
{
    EV_DETAIL << "Processing ACK in a data transfer state\n";

    //swift
    auto packet_dup = packet->dup();
    packet_dup->setFrontIteratorPosition(b(0));
    packet_dup->removeAtFront<EthernetPhyHeader>();
    auto eth_header = packet_dup->peekAtFront<EthernetMacHeader>();
    simtime_t remote_queueing_delay = eth_header->getRemote_queueing_time();
    simtime_t local_nic_rx_delay = eth_header->getLocal_nic_rx_delay();
    state->hop_count = tcpseg->getHop_count_to_be_echoed();
    EV << "receivedDataAck called. LocalRxDelay is: " << local_nic_rx_delay << " and remoteQueueingDelay is: " <<
            remote_queueing_delay << endl;
    EV << "The echoed hop count is " << state->hop_count << endl;
    state->end_point_delay = remote_queueing_delay + local_nic_rx_delay;
    delete packet_dup;

    int payloadLength = packet->getByteLength() - B(tcpseg->getHeaderLength()).get();

    //ECN
    TcpStateVariables* state = getState();
    if (state && state->ect) {
        if (tcpseg->getEceBit() == true) {
            EV_INFO << "Received packet with ECE\n";
            state->gotEce = true;
        }

    }

    //
    //"
    //  If SND.UNA < SEG.ACK =< SND.NXT then, set SND.UNA <- SEG.ACK.
    //  Any segments on the retransmission queue which are thereby
    //  entirely acknowledged are removed.  Users should receive
    //  positive acknowledgments for buffers which have been SENT and
    //  fully acknowledged (i.e., SEND buffer should be returned with
    //  "ok" response).  If the ACK is a duplicate
    //  (SEG.ACK < SND.UNA), it can be ignored.  If the ACK acks
    //  something not yet sent (SEG.ACK > SND.NXT) then send an ACK,
    //  drop the segment, and return.
    //
    //  If SND.UNA < SEG.ACK =< SND.NXT, the send window should be
    //  updated.  If (SND.WL1 < SEG.SEQ or (SND.WL1 = SEG.SEQ and
    //  SND.WL2 =< SEG.ACK)), set SND.WND <- SEG.WND, set
    //  SND.WL1 <- SEG.SEQ, and set SND.WL2 <- SEG.ACK.
    //
    //  Note that SND.WND is an offset from SND.UNA, that SND.WL1
    //  records the sequence number of the last segment used to update
    //  SND.WND, and that SND.WL2 records the acknowledgment number of
    //  the last segment used to update SND.WND.  The check here
    //  prevents using old segments to update the window.
    //"
    // Note: should use SND.MAX instead of SND.NXT in above checks
    //
    if (seqGE(state->snd_una, tcpseg->getAckNo())) {
        //
        // duplicate ACK? A received TCP segment is a duplicate ACK if all of
        // the following apply:
        //    (1) snd_una == ackNo
        //    (2) segment contains no data
        //    (3) there's unacked data (snd_una != snd_max)
        //
        // Note: ssfnet uses additional constraint "window is the same as last
        // received (not an update)" -- we don't do that because window updates
        // are ignored anyway if neither seqNo nor ackNo has changed.
        //
        if (state->snd_una == tcpseg->getAckNo() && payloadLength == 0 && state->snd_una != state->snd_max) {
            state->dupacks++;

            emit(dupAcksSignal, tcpseg->getAckNo());
            tcpMain->num_dup_acks++;

            // we need to update send window even if the ACK is a dupACK, because rcv win
            // could have been changed if faulty data receiver is not respecting the "do not shrink window" rule
            updateWndInfo(tcpseg);

            tcpAlgorithm->receivedDuplicateAck();
        }
        else {
            // if doesn't qualify as duplicate ACK, just ignore it.
            if (payloadLength == 0) {
                if (state->snd_una != tcpseg->getAckNo())
                    EV_DETAIL << "Old ACK: ackNo < snd_una\n";
                else if (state->snd_una == state->snd_max)
                    EV_DETAIL << "ACK looks duplicate but we have currently no unacked data (snd_una == snd_max)\n";
            }

            // reset counter
            state->dupacks = 0;

//            emit(dupAcksSignal, state->dupacks);
        }
    }
    else if (seqLE(tcpseg->getAckNo(), state->snd_max)) {
        // ack in window.
        uint32 old_snd_una = state->snd_una;
        state->snd_una = tcpseg->getAckNo();

        emit(unackedSignal, state->snd_max - state->snd_una);

        // after retransmitting a lost segment, we may get an ack well ahead of snd_nxt
        if (seqLess(state->snd_nxt, state->snd_una))
            state->snd_nxt = state->snd_una;

        // RFC 1323, page 36:
        // "If SND.UNA < SEG.ACK =< SND.NXT then, set SND.UNA <- SEG.ACK.
        // Also compute a new estimate of round-trip time.  If Snd.TS.OK
        // bit is on, use my.TSclock - SEG.TSecr; otherwise use the
        // elapsed time since the first segment in the retransmission
        // queue was sent.  Any segments on the retransmission queue
        // which are thereby entirely acknowledged."
        if (state->ts_enabled)
            tcpAlgorithm->rttMeasurementCompleteUsingTS(getTSecr(tcpseg));
        // Note: If TS is disabled the RTT measurement is completed in TcpBaseAlg::receivedDataAck()

        uint32 discardUpToSeq = state->snd_una;

        // our FIN acked?
        if (state->send_fin && tcpseg->getAckNo() == state->snd_fin_seq + 1) {
            // set flag that our FIN has been acked
            EV_DETAIL << "SEPEHR: ACK acks our FIN\n";

            const auto& packetDup = packet->dup();
            packetDup->setFrontIteratorPosition(b(0));
            packetDup->removeAtFront<EthernetPhyHeader>();
            packetDup->removeAtFront<EthernetMacHeader>();
            auto ipv4Header = packetDup->peekAtFront<Ipv4Header>();
            // The source of SYN is unknown, so we don't use the dest in hash
            std::string src_ip = ipv4Header->getSourceAddress().str();
            std::string conn_id = std::to_string(this->getId());
            std::hash<std::string> flow_hash;
            long hash_of_flow = flow_hash(src_ip + conn_id);
            EV << "Fyn-Ack received, the info for flow hash is " <<
                    src_ip + conn_id << endl;
            EV << "Hash of flow is " << hash_of_flow << endl;
            emit(Tcp::tcpConnectionFINRcvdSignal, hash_of_flow);
            delete packetDup;

            state->fin_ack_rcvd = true;
            discardUpToSeq--;    // the FIN sequence number is not real data
        }

        // acked data no longer needed in send queue
        sendQueue->discardUpTo(discardUpToSeq);

        // acked data no longer needed in rexmit queue
        if (state->sack_enabled)
            rexmitQueue->discardUpTo(discardUpToSeq);

        updateWndInfo(tcpseg);

        // if segment contains data, wait until data has been forwarded to app before sending ACK,
        // otherwise we would use an old ACKNo
        if (payloadLength == 0 && fsm.getState() != TCP_S_SYN_RCVD) {
            state->last_ack_packet_rcvd = packet->dup();
            // notify
            tcpAlgorithm->receivedDataAck(old_snd_una);
            delete state->last_ack_packet_rcvd;

            // in the receivedDataAck we need the old value
            state->dupacks = 0;

//            emit(dupAcksSignal, state->dupacks);
        }
    }
    else {
        ASSERT(seqGreater(tcpseg->getAckNo(), state->snd_max));    // from if-ladder

        // send an ACK, drop the segment, and return.
        tcpAlgorithm->receivedAckForDataNotYetSent(tcpseg->getAckNo());
        state->dupacks = 0;

//        emit(dupAcksSignal, state->dupacks);

        return false;    // means "drop"
    }

    return true;
}

//----

void TcpConnection::process_TIMEOUT_CONN_ESTAB()
{
    switch (fsm.getState()) {
        case TCP_S_SYN_RCVD:
        case TCP_S_SYN_SENT:
            // Nothing to do here. TIMEOUT_CONN_ESTAB event will automatically
            // take the connection to LISTEN or CLOSED, and cancel SYN-REXMIT timer.
            if (state->active) {
                // notify user if we're on the active side
                sendIndicationToApp(TCP_I_TIMED_OUT);
            }
            break;

        default:
            // We should not receive this timeout in this state.
            throw cRuntimeError(tcpMain, "Internal error: received CONN_ESTAB timeout in state %s",
                stateName(fsm.getState()));
    }
}

void TcpConnection::process_TIMEOUT_2MSL()
{
    //"
    // If the time-wait timeout expires on a connection delete the TCB,
    // enter the CLOSED state and return.
    //"
    switch (fsm.getState()) {
        case TCP_S_TIME_WAIT:
            // Nothing to do here. The TIMEOUT_2MSL event will automatically take
            // the connection to CLOSED. We already notified the user
            // (TCP_I_CLOSED) when we entered the TIME_WAIT state from CLOSING,
            // FIN_WAIT_1 or FIN_WAIT_2.
            break;

        default:
            // We should not receive this timeout in this state.
            throw cRuntimeError(tcpMain,
                "Internal error: received time-wait (2MSL) timeout in state %s",
                stateName(fsm.getState()));
    }
}

void TcpConnection::process_TIMEOUT_FIN_WAIT_2()
{
    switch (fsm.getState()) {
        case TCP_S_FIN_WAIT_2:
            // Nothing to do here. The TIMEOUT_FIN_WAIT_2 event will automatically take
            // the connection to CLOSED.
            break;

        default:
            // We should not receive this timeout in this state.
            throw cRuntimeError(tcpMain, "Internal error: received FIN_WAIT_2 timeout in state %s",
                stateName(fsm.getState()));
    }
}

void TcpConnection::startSynRexmitTimer()
{
    state->syn_rexmit_count = 0;
    state->syn_rexmit_timeout = TCP_TIMEOUT_SYN_REXMIT;

    if (synRexmitTimer->isScheduled())
        cancelEvent(synRexmitTimer);

    scheduleTimeout(synRexmitTimer, state->syn_rexmit_timeout);
}

void TcpConnection::process_TIMEOUT_SYN_REXMIT(TcpEventCode& event)
{
    std::string algorithm_name = getAncestorPar("tcpAlgorithmClass");
    if (++state->syn_rexmit_count > MAX_SYN_REXMIT_COUNT) {
        EV_INFO << "Retransmission count during connection setup exceeds " << MAX_SYN_REXMIT_COUNT << ", giving up\n";
        // Note ABORT will take the connection to closed, and cancel CONN-ESTAB timer as well
        event = TCP_E_ABORT;
        return;
    }

    if (algorithm_name.compare("TcpPFabric") == 0) {
        state->syn_rexmit_count = std::min(state->syn_rexmit_count, MAX_SYN_REXMIT_COUNT - 3);
    }

    EV_INFO << "Performing retransmission #" << state->syn_rexmit_count << "\n";

    // resend what's needed
    switch (fsm.getState()) {
        case TCP_S_SYN_SENT:
            sendSyn();
            break;

        case TCP_S_SYN_RCVD:
            sendSynAck();
            break;

        default:
            throw cRuntimeError(tcpMain, "Internal error: SYN-REXMIT timer expired while in state %s",
                stateName(fsm.getState()));
    }

    // reschedule timer
    state->syn_rexmit_timeout *= 2;

    if (state->syn_rexmit_timeout > TCP_TIMEOUT_SYN_REXMIT_MAX)
        state->syn_rexmit_timeout = TCP_TIMEOUT_SYN_REXMIT_MAX;

    scheduleTimeout(synRexmitTimer, state->syn_rexmit_timeout);
}

//
//TBD:
//"
// USER TIMEOUT
//
//    For any state if the user timeout expires, flush all queues, signal
//    the user "error:  connection aborted due to user timeout" in general
//    and for any outstanding calls, delete the TCB, enter the CLOSED
//    state and return.
//"

} // namespace tcp
} // namespace inet

