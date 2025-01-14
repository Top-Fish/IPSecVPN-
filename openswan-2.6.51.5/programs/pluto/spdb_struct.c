/* Security Policy Data Base (such as it is)
 * Copyright (C) 1998-2001  D. Hugh Redelmeier.
 * Copyright (C) 2003-2007 Michael Richardson <mcr@xelerance.com>
 * Copyright (C) 2003-2008 Paul Wouters <paul@xelerance.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <openswan.h>
#include <openswan/ipsec_policy.h>
#include "openswan/pfkeyv2.h"

#include "sysdep.h"
#include "constants.h"
#include "oswlog.h"

#include "defs.h"
#include "id.h"
#include "x509.h"
#include "pgp.h"
#include "certs.h"
#ifdef XAUTH_USEPAM
#include <security/pam_appl.h>
#endif
#include "pluto/connections.h"	/* needs id.h */
#include "state.h"
#include "packet.h"
#include "keys.h"
#include "secrets.h"
#include "kernel.h"	/* needs connections.h */
#include "log.h"
#include "spdb.h"
#include "whack.h"	/* for RC_LOG_SERIOUS */
#include "plutoalg.h"

#include "sha1.h"
#include "md5.h"
#include "crypto.h" /* requires sha1.h and md5.h */

#include "alg_info.h"
#include "kernel_alg.h"
#include "ike_alg.h"
#include "db_ops.h"

#ifdef NAT_TRAVERSAL
#include "nat_traversal.h"
#endif

/*
 * empty structure, for clone use.
 */
static struct db_attr otempty[] = {
	{ .type.oakley=OAKLEY_ENCRYPTION_ALGORITHM, -1 },
	{ .type.oakley=OAKLEY_HASH_ALGORITHM,       -1 },
	{ .type.oakley=OAKLEY_AUTHENTICATION_METHOD, -1 },
	{ .type.oakley=OAKLEY_GROUP_DESCRIPTION,    -1 },
	{ .type.oakley=OAKLEY_KEY_LENGTH,    -1 },
	};

static struct db_trans oakley_trans_empty[] = {
    { AD_TR(KEY_IKE, otempty) },
};

static struct db_prop oakley_pc_empty[] =
{ { AD_PR(PROTO_ISAKMP, oakley_trans_empty) } };

static struct db_prop_conj oakley_props_empty[] = {{ AD_PC(oakley_pc_empty) }};

struct db_sa oakley_empty = { AD_SAp(oakley_props_empty) };

/*
 * 	Create an OAKLEY proposal based on alg_info and policy
 *
 * Note: maxtrans is an enum, not a count
 * 	Should probably be declared an enum at some point.
 * 	-1 - ???
 * 	 0 - No limit
 * 	 1 - One proposal - period
 * 	 2 - One DH group, take first DH group and ignore any that don't match
 *
 */
struct db_sa *
oakley_alg_makedb(struct alg_info_ike *ai
		  , struct db_sa *base
		  , int maxtrans)
{
    /* struct db_context inprog UNUSED; */
    struct db_sa *gsp = NULL;
    struct db_sa *emp_sp = NULL;
    struct ike_info *ike_info;
    unsigned ealg, halg, modp, eklen=0;
    /* Next two are for multiple proposals in agressive mode... */
    unsigned last_modp=0, wrong_modp=0;
    struct encrypt_desc *enc_desc;
    int transcnt = 0;
    int i;

    /*
     * start by copying the proposal that would have been picked by
     * standard defaults.
     */

    if (!ai) {
	DBG(DBG_CRYPT,DBG_log("no IKE algorithms for this connection "));

	return NULL;
    }

    gsp = NULL;

    /*
     * for each group, we will create a new proposal item, and then
     * append it to the list of transforms in the conjoint point.
     *
     * when creating each item, we will use the first transform
     * from the base item as the template.
     */
    ALG_INFO_IKE_FOREACH(ai, ike_info, i) {/*遍历算法*/

	if(ike_info->ike_default == FALSE) {/*配置下发了建议载荷，不需要使用默认的SA*/
	    struct db_attr  *enc, *hash, *auth, *grp, *enc_keylen, *new_auth;
	    struct db_trans *trans;
	    struct db_prop  *prop;
	    struct db_prop_conj *cprop;

		/*获取到加密算法、哈希算法、认证算法、加密秘钥长度等信息*/
	    ealg = ike_info->ike_ealg;
	    halg = ike_info->ike_halg;
	    modp = ike_info->ike_modp;
	    eklen= ike_info->ike_eklen;
		
#if 1  /*判断这几个算法是否合法、是否存在等*/
	    if (!ike_alg_enc_present(ealg)) {
		DBG_log("oakley_alg_makedb() "
			"ike enc ealg=%d not present",
			ealg);
		continue;
	    }
	    if (!ike_alg_hash_present(halg)) {
		DBG_log("oakley_alg_makedb() "
			"ike hash halg=%d not present",
			halg);
		continue;
	    }
	    enc_desc = ike_alg_get_encrypter(ealg);

	    passert(enc_desc != NULL);/*确定密钥长度是否合法*/
	    if (eklen
		&& (eklen < enc_desc->keyminlen
		    || eklen >  enc_desc->keymaxlen))

		{
		    DBG_log("ike_alg_db_new() "
			    "ealg=%d (specified) keylen:%d, "
			    "not valid "
			    "min=%d, max=%d"
			    , ealg
			    , eklen
			    , enc_desc->keyminlen
			    , enc_desc->keymaxlen
			    );
		    continue;
		}
#endif
	    /* okay copy the basic item, and modify it. */
	    if(eklen > 0)
	    {
		emp_sp = sa_copy_sa(&oakley_empty, 0);/*重新分配一个新的描述信息*/
		cprop = &base->prop_conjs[0];/*从定义的描述信息中获取参数*/
		prop = &cprop->props[0];/*建议载荷*/
		trans = &prop->trans[0];/*变换载荷*/
		new_auth = &trans->attrs[2];/*属性载荷*/
		/*上面主要为了获取auth算法*/


		cprop = &emp_sp->prop_conjs[0];
		prop = &cprop->props[0];
		trans = &prop->trans[0];
		auth = &trans->attrs[2];
		*auth = *new_auth;		/*给新的描述结构中设置认证算法*/
	    }
	    else
		emp_sp = sa_copy_sa_first(base);

	    passert(emp_sp->prop_conj_cnt == 1);
	    cprop = &emp_sp->prop_conjs[0];

	    passert(cprop->prop_cnt == 1);
	    prop = &cprop->props[0];

	    passert(prop->trans_cnt == 1);
	    trans = &prop->trans[0];

	    passert(trans->attr_cnt == 4 || trans->attr_cnt == 5);
	    enc  = &trans->attrs[0]; /*加密*/
	    hash = &trans->attrs[1];/*哈希*/
	    auth = &trans->attrs[2];/*认证*/
	    grp  = &trans->attrs[3];/*DH组?*/

	    if(eklen > 0) {
		enc_keylen = &trans->attrs[4];
		enc_keylen->val = eklen;/*设置加密算法长度*/
	    } else
		trans->attr_cnt = 4;

	    passert(enc->type.oakley == OAKLEY_ENCRYPTION_ALGORITHM);
	    if(ealg > 0) {
		enc->val = ealg;/*设置加密算法*/
	    }

	    modp = ike_info->ike_modp;
	    eklen= ike_info->ike_eklen;

	    passert(hash->type.oakley == OAKLEY_HASH_ALGORITHM);
	    if(halg > 0) {
		hash->val = halg;/*设置哈希算法*/
	    }

	    passert(auth->type.oakley == OAKLEY_AUTHENTICATION_METHOD);
	    /* no setting for auth type for IKE */

	    passert(grp->type.oakley  == OAKLEY_GROUP_DESCRIPTION);
	    if(modp > 0) {
		grp->val = modp;  /*设置认证算法，MODP和Group指的是同一个参数*/
	    }
	} else {
	    emp_sp = sa_copy_sa(base, 0);
	}

	if(maxtrans == 1) {/*相应方用来选择对端的建议载荷时使用此流程*/
            /*
             *  We're going to leave maxtrans == 1 alone in case there
             * really really is a case where we only want 1.
             */

	    if(transcnt == 0) {/*之前尚未匹配到合适的SA算法信息，当前的算法满足要求*/
		DBG(DBG_CONTROL, DBG_log("using transform (%d,%d,%d,%ld)"
					 , ike_info->ike_ealg
					 , ike_info->ike_halg
					 , ike_info->ike_modp
					 , (long)ike_info->ike_eklen));
		if(gsp) {
		    free_sa(gsp);
		}
		gsp = emp_sp;
	    } else {/*之前已经有满足要求的SA算法，则使用之前的SA,并丢弃当前的SA*/
		free_sa(emp_sp);
	    }

	    if(transcnt > 0) {
			if(transcnt == 1) {
			    loglog(RC_LOG_SERIOUS

				   , "multiple transforms were set in aggressive mode. Only first one used.");
			}

			loglog(RC_LOG_SERIOUS
			       , "transform (%d,%d,%d,%ld) ignored."
			       , ike_info->ike_ealg
			       , ike_info->ike_halg
			       , ike_info->ike_modp
			       , (long)ike_info->ike_eklen);
	    }

	} else {/*发起方用来构建建议载荷时一般使用此流程*/
            /*
     * Now...  We're allowing multiple proposals...  Are we allowing
     * multiple DH groups?
     */

	    struct db_sa *new;

		/*如果是两个变换载荷，则modp必须一样???*/
            if(maxtrans == 2 && transcnt > 0 && ike_info->ike_modp != last_modp ) {
                /* Not good.
                 * Already got a DH group and this one doesn't match */
		if(wrong_modp == 0) {
		    loglog(RC_LOG_SERIOUS
			   , "multiple DH groups were set in aggressive mode. Only first one used.");
		}

		loglog(RC_LOG_SERIOUS
		           , "transform (%d,%d,%d,%ld) ignored."
		           , ike_info->ike_ealg
		           , ike_info->ike_halg
		           , ike_info->ike_modp
		           , (long)ike_info->ike_eklen);

                wrong_modp++;

		free_sa(emp_sp);
	    } else if(gsp) {
	    /* now merge emp_sa and gsp */
		new = sa_merge_proposals(gsp, emp_sp);
		free_sa(gsp);
		free_sa(emp_sp);
		emp_sp = NULL;
		gsp = new;
	    } else {
		gsp = emp_sp;
	    }
            last_modp = ike_info->ike_modp;
	}
	transcnt++;
    }
    gsp->parentSA = TRUE;

    return gsp;
}


/*
 * Local Variables:
 * c-style: pluto
 * c-basic-offset: 4
 * End:
 */
