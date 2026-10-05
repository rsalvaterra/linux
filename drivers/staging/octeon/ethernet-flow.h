/* SPDX-License-Identifier: GPL-2.0 */
#ifndef OCTEON_ETHERNET_FLOW_H
#define OCTEON_ETHERNET_FLOW_H

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/pkt_cls.h>

struct cvmx_wqe;

/* Starts at 0. Set while a hardware flowtable is bound on a CN50xx. */
extern int cvm_oct_flow_shortcut;

bool cvm_oct_flow_receive(struct net_device *dev, struct sk_buff *skb,
			  const struct cvmx_wqe *work);
int cvm_oct_setup_tc(struct net_device *dev, enum tc_setup_type type,
		     void *type_data);

#endif
