//
// Copyright (C) OpenSim Ltd.
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
// along with this program; if not, see http://www.gnu.org/licenses/.
//

package dc_simulations.modules.V2;

import inet.queueing.queue.PacketQueue;
import inet.queueing.contract.IPacketQueue;

// NOTE: Using V2 pifo is a little bit different than normal queues, 
// because in most cases of normal queues we drop the packet
// in relay unit but v2pifo drops them in queue 

// Instead of implementing pure scheduling, here we use a specific
// number of fifo queues for approximation

simple V2PIFOPrioQueue extends PacketQueue
{
	parameters:
        @class(V2PIFOPrioQueue);
        
        int per_queue_packetCapacity = default(-1);
		int per_queue_dataCapacity @unit(b) = default(-1b);
        
        // dctcp_support
        int queue_num = default(-1);
        int dctcp_thresh = default(-1);
        string where_to_mark_packets = default("enqueue");		// enqueue or dequeue
        int deflection_threshold = default(-1);	// this should be in bit if you're using dataCapacity
        string priority_mapping_scheme = default("");
        
        // AIFO
        double aifo_k = default(-1);
        int quantile_wind_size = default(-1);
        int aifo_sample_count = default(-1);
        
        // RED
        // We got the following values from DCTCP paper
        double red_wq = default(0.002);  // weight of the current queue length in the averaged queue length
        double red_minth = default(75000);  // dctcp_value = 50 packets, 75000 bytes
        double red_maxth = default(225000);  // dctcp_value = 150 packets, 225000 bytes
        double red_maxp = default(0.1);  // maximum value for pbs
        double red_pkrate = default(150);  // average packet rate for calculations when queue is empty
        
        // WRED (The following are for packets with higher priorities)
        bool deploy_wred = default(false);
        int rank_grouping_cut_off = default(40000);
        double hp_red_wq = default(0.002);  // weight of the current queue length in the averaged queue length
        double hp_red_minth = default(75000);  // dctcp_value = 50 packets, 75000 bytes
        double hp_red_maxth = default(225000);  // dctcp_value = 150 packets, 225000 bytes
        double hp_red_maxp = default(0.1);  // maximum value for pbs
        double hp_red_pkrate = default(150);  // average packet rate for calculations when queue is empty
        
        bool just_prioritize_bursty_flows = default(false);
        
        ////@signal[packetDropSeq]();
        ////@signal[packetDropRetCount]();
        ////@signal[packetDropTotalPayloadLength]();
        
        ////@statistic[droppktSeqs](title="seqs of the dropped packets"; source="packetDropSeq"; record=vector; interpolationmode=none);
        ////@statistic[droppktRetCount](title="ret count of the dropped packets"; source="packetDropRetCount"; record=vector; interpolationmode=none);
        ////@statistic[pktDropTotalPayloadLen](title="total payload length of BG packets dropped in v2 queue"; source="packetDropTotalPayloadLength"; record=vector; interpolationmode=none);
}
