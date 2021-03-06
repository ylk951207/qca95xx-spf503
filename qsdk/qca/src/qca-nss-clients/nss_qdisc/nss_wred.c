/*
 **************************************************************************
 * Copyright (c) 2014-2017 The Linux Foundation. All rights reserved.
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 **************************************************************************
 */

#include "nss_qdisc.h"

#define NSS_WRED_SUPPORT_TRAFFIC_CLASS 6
#define NSS_WRED_MAX_TRAFFIC_CLASS NSS_WRED_SUPPORT_TRAFFIC_CLASS+1

/*
 * nsswred traffic class structure
 */
struct nss_wred_traffic_class {
	u32 limit;			/* Queue length */
	u32 weight_mode_value;		/* Weight mode value */
	struct tc_red_alg_parameter rap;/* Parameters for RED alg */
};

/*
 * nsswred private qdisc structure
 */
struct nss_wred_sched_data {
	struct nss_qdisc nq;			/* Common base class for all nss qdiscs */
	u32 traffic_classes;			/* # of traffic classs in this wred*/
	u32 def_traffic_class;			/* Default traffic class if no match */
	enum tc_nsswred_weight_modes weight_mode;	/* Weight mode */
	struct nss_wred_traffic_class nwtc[NSS_WRED_MAX_TRAFFIC_CLASS];
						/* Parameters for each traffic class */
	u8 ecn;					/* Mark ECN or drop pkt */
	u8 weighted;				/* This is a wred or red */
};

/*
 * nss_wred_enqueue()
 *	Enqueue API for nsswred qdisc
 */
static int nss_wred_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	return nss_qdisc_enqueue(skb, sch);
}

/*
 * nss_wred_dequeue()
 *	Dequeue API for nsswred qdisc
 */
static struct sk_buff *nss_wred_dequeue(struct Qdisc *sch)
{
	return nss_qdisc_dequeue(sch);
}

/*
 * nss_wred_drop()
 *	Drops a packet from HLOS queue.
 */
static unsigned int nss_wred_drop(struct Qdisc *sch)
{
	nss_qdisc_info("nsswred dropping");
	return nss_qdisc_drop(sch);
}

/*
 * nss_wred_reset()
 *	Reset the nsswred qdisc
 */
static void nss_wred_reset(struct Qdisc *sch)
{
	nss_qdisc_info("nsswred resetting!");
	nss_qdisc_reset(sch);
}

/*
 * nss_wred_destroy()
 *	Destroy the nsswred qdisc
 */
static void nss_wred_destroy(struct Qdisc *sch)
{
	struct nss_qdisc *nq = (struct nss_qdisc *)qdisc_priv(sch);

	/*
	 * Stop the polling of basic stats
	 */
	nss_qdisc_stop_basic_stats_polling(nq);

	nss_qdisc_destroy(nq);
	nss_qdisc_info("nsswred destroyed");
}

/*
 * nsswred policy structure
 */
static const struct nla_policy nss_wred_policy[TCA_NSSWRED_MAX + 1] = {
	[TCA_NSSWRED_PARMS] = { .len = sizeof(struct tc_nsswred_qopt) },
};

/*
 * nss_wred_change()
 *	Function call to configure the nsswred parameters
 */
static int nss_wred_change(struct Qdisc *sch, struct nlattr *opt)
{
	struct nss_wred_sched_data *q;
	struct nlattr *na[TCA_NSSWRED_MAX + 1];
	struct tc_nsswred_qopt *qopt;
	int err;
	struct nss_if_msg nim;

	q = qdisc_priv(sch);

	if (opt == NULL) {
		return -EINVAL;
	}
	err = nla_parse_nested(na, TCA_NSSWRED_MAX, opt, nss_wred_policy);
	if (err < 0) {
		return err;
	}
	if (na[TCA_NSSWRED_PARMS] == NULL) {
		return -EINVAL;
	}
	qopt = nla_data(na[TCA_NSSWRED_PARMS]);

	nss_qdisc_info("%s: nsswred %x traffic_classes:%d def_traffic_class: %d Weight_Mode:%d ECN:%d\n",
			__func__, sch->handle, qopt->traffic_classes, qopt->def_traffic_class, qopt->weight_mode, qopt->ecn);

	if (qopt->traffic_classes) {
		/*
		 * This is a wred setup command, do checks again because parameters might not come from tc utility
		 */
		if (qopt->traffic_classes > NSS_WRED_SUPPORT_TRAFFIC_CLASS) {
			nss_qdisc_error("%s: nsswred %x traffic classes should not exceeds %d\n", __func__, sch->handle, NSS_WRED_SUPPORT_TRAFFIC_CLASS);
			return -EINVAL;
		}
		if (qopt->def_traffic_class < 1 || qopt->def_traffic_class > qopt->traffic_classes) {
			nss_qdisc_error("%s: nsswred %x invalid default traffic\n", __func__, sch->handle);
			return -EINVAL;
		}
		if (qopt->weight_mode >= TC_NSSWRED_WEIGHT_MODES) {
			nss_qdisc_error("%s: nsswred %x invalid weight_mode\n", __func__, sch->handle);
			return -EINVAL;
		}
		q->traffic_classes = qopt->traffic_classes ;
		q->def_traffic_class = qopt->def_traffic_class;
		q->weight_mode = qopt->weight_mode;
		q->ecn = qopt->ecn;
		q->weighted = 1;
	} else {
		if (qopt->traffic_id) {
			/*
			 * This is a wred traffic class command
			 */
			if (!q->traffic_classes) {
				nss_qdisc_error("%s: nsswred %x not setup yet, can't accept traffic class configuration\n", __func__, sch->handle);
				return -EINVAL;
			}
			if (!qopt->limit || !qopt->rap.min || !qopt->rap.max || !qopt->weight_mode_value || !qopt->rap.exp_weight_factor) {
				nss_qdisc_error("%s: nsswred %x Requires RED algorithm parameters and weight_mode_value\n", __func__, sch->handle);
				return -EINVAL;
			}
		} else {
			/*
			 * This is a red setup command
			 */
			if (!qopt->limit || !qopt->rap.exp_weight_factor) {
				nss_qdisc_error("%s: nsswred %x Requires RED algorithm parameters\n", __func__, sch->handle);
				return -EINVAL;
			}
			/*
			 * If min/max does not specify, calculated it
			 */
			if (!qopt->rap.max) {
				qopt->rap.max = qopt->rap.min ? qopt->rap.min * 3 : qopt->limit / 4;
			}
			if (!qopt->rap.min) {
				qopt->rap.min = qopt->rap.max / 3;
			}
			q->ecn = qopt->ecn;
		}
		q->nwtc[qopt->traffic_id].limit = qopt->limit;
		q->nwtc[qopt->traffic_id].weight_mode_value = qopt->weight_mode_value;
		q->nwtc[qopt->traffic_id].rap.min = qopt->rap.min;
		q->nwtc[qopt->traffic_id].rap.max = qopt->rap.max;
		q->nwtc[qopt->traffic_id].rap.probability = qopt->rap.probability;
		q->nwtc[qopt->traffic_id].rap.exp_weight_factor = qopt->rap.exp_weight_factor;
	}

	nim.msg.shaper_configure.config.msg.shaper_node_config.qos_tag = q->nq.qos_tag;
	nim.msg.shaper_configure.config.msg.shaper_node_config.snc.wred_param.limit = qopt->limit;
	nim.msg.shaper_configure.config.msg.shaper_node_config.snc.wred_param.weight_mode = qopt->weight_mode;
	nim.msg.shaper_configure.config.msg.shaper_node_config.snc.wred_param.weight_mode_value = qopt->weight_mode_value;
	nim.msg.shaper_configure.config.msg.shaper_node_config.snc.wred_param.rap.min = qopt->rap.min;
	nim.msg.shaper_configure.config.msg.shaper_node_config.snc.wred_param.rap.max = qopt->rap.max;
	nim.msg.shaper_configure.config.msg.shaper_node_config.snc.wred_param.rap.probability = qopt->rap.probability;
	nim.msg.shaper_configure.config.msg.shaper_node_config.snc.wred_param.rap.exp_weight_factor = qopt->rap.exp_weight_factor;
	nim.msg.shaper_configure.config.msg.shaper_node_config.snc.wred_param.traffic_classes = qopt->traffic_classes;
	nim.msg.shaper_configure.config.msg.shaper_node_config.snc.wred_param.ecn = qopt->ecn;
	nim.msg.shaper_configure.config.msg.shaper_node_config.snc.wred_param.def_traffic_class = qopt->def_traffic_class;
	nim.msg.shaper_configure.config.msg.shaper_node_config.snc.wred_param.traffic_id = qopt->traffic_id;

	if (nss_qdisc_configure(&q->nq, &nim, NSS_SHAPER_CONFIG_TYPE_SHAPER_NODE_CHANGE_PARAM) < 0) {
		nss_qdisc_error("%s: nsswred %x configuration failed\n", __func__, sch->handle);
		return -EINVAL;
	}

	if (qopt->set_default == 0)
		return 0;

	/*
	 * Set this qdisc to be the default qdisc for enqueuing packets.
	*/
	if (nss_qdisc_set_default(&q->nq) < 0) {
		nss_qdisc_error("%s: nsswred %x set_default failed\n", __func__, sch->handle);
		return -EINVAL;
	}

	nss_qdisc_info("%s: nsswred queue (qos_tag:%u) set as default\n", __func__, q->nq.qos_tag);

	return 0;
}

/*
 * nss_wred_init()
 *	Init the nsswred qdisc
 */
static int nss_wred_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct nss_qdisc *nq = qdisc_priv(sch);

	if (opt == NULL)
		return -EINVAL;

	nss_qdisc_info("Initializing Wred - type %d\n", NSS_SHAPER_NODE_TYPE_WRED);
	nss_wred_reset(sch);

	if (nss_qdisc_init(sch, nq, NSS_QDISC_MODE_NSS, NSS_SHAPER_NODE_TYPE_WRED, 0) < 0)
		return -EINVAL;

	nss_qdisc_info("NSS wred initialized - handle %x parent %x\n", sch->handle, sch->parent);
	if (nss_wred_change(sch, opt) < 0) {
		nss_qdisc_destroy(nq);
		return -EINVAL;
	}

	/*
	 * Start the stats polling timer
	 */
	nss_qdisc_start_basic_stats_polling(nq);

	return 0;
}

/*
 * nss_wred_dump()
 *	Dump the parameters of nsswred to tc
 */
static int nss_wred_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct nss_wred_sched_data *q;
	struct nlattr *opts = NULL;
	struct tc_nsswred_qopt opt;
	int i;
	nss_qdisc_info("Nsswred Dumping!");

	q = qdisc_priv(sch);
	if (q == NULL) {
		return -1;
	}

	if (q->weighted) {
		opt.traffic_classes = q->traffic_classes;
		opt.def_traffic_class = q->def_traffic_class;
		opt.ecn = q->ecn;
		opt.weight_mode = q->weight_mode;
		for (i = 0 ; i < q->traffic_classes; i++) {
			opt.tntc[i].limit = q->nwtc[i+1].limit;
			opt.tntc[i].weight_mode_value = q->nwtc[i+1].weight_mode_value;
			opt.tntc[i].rap.exp_weight_factor = q->nwtc[i+1].rap.exp_weight_factor;
			opt.tntc[i].rap.min = q->nwtc[i+1].rap.min;
			opt.tntc[i].rap.max = q->nwtc[i+1].rap.max;
			opt.tntc[i].rap.probability = q->nwtc[i+1].rap.probability;
		}
	} else {
		opt.ecn = q->ecn;
		opt.limit = q->nwtc[0].limit;
		opt.rap.min = q->nwtc[0].rap.min;
		opt.rap.max = q->nwtc[0].rap.max;
		opt.rap.exp_weight_factor = q->nwtc[0].rap.exp_weight_factor;
		opt.rap.probability = q->nwtc[0].rap.probability;
	}

	opts = nla_nest_start(skb, TCA_OPTIONS);
	if (opts == NULL || nla_put(skb, TCA_NSSWRED_PARMS, sizeof(opt), &opt)) {
		goto nla_put_failure;
	}

	return nla_nest_end(skb, opts);

nla_put_failure:
	nla_nest_cancel(skb, opts);
	return -EMSGSIZE;
}

/*
 * nss_wred_peek()
 *	Peeks the first packet in queue for this qdisc
 */
static struct sk_buff *nss_wred_peek(struct Qdisc *sch)
{
	nss_qdisc_info("Nsswred Peeking");
	return nss_qdisc_peek(sch);
}

/*
 * Registration structure for nss_red qdisc
 */
struct Qdisc_ops nss_red_qdisc_ops __read_mostly = {
	.id		=	"nssred",
	.priv_size	=	sizeof(struct nss_wred_sched_data),
	.enqueue	=	nss_wred_enqueue,
	.dequeue	=	nss_wred_dequeue,
	.peek		=	nss_wred_peek,
	.drop		=	nss_wred_drop,
	.init		=	nss_wred_init,
	.reset		=	nss_wred_reset,
	.destroy	=	nss_wred_destroy,
	.change		=	nss_wred_change,
	.dump		=	nss_wred_dump,
	.owner		=	THIS_MODULE,
};

/*
 * Registration structure for nss_wred qdisc
 */
struct Qdisc_ops nss_wred_qdisc_ops __read_mostly = {
	.id		=	"nsswred",
	.priv_size	=	sizeof(struct nss_wred_sched_data),
	.enqueue	=	nss_wred_enqueue,
	.dequeue	=	nss_wred_dequeue,
	.peek		=	nss_wred_peek,
	.drop		=	nss_wred_drop,
	.init		=	nss_wred_init,
	.reset		=	nss_wred_reset,
	.destroy	=	nss_wred_destroy,
	.change		=	nss_wred_change,
	.dump		=	nss_wred_dump,
	.owner		=	THIS_MODULE,
};
