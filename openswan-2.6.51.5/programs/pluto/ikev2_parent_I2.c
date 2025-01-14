/*
 * IKEv2 parent SA creation routines --- outI1 routines
 * Copyright (C) 2007-2017 Michael Richardson <mcr@xelerance.com>
 * Copyright (C) 2008-2011 Paul Wouters <paul@xelerance.com>
 * Copyright (C) 2008 Antony Antony <antony@xelerance.com>
 * Copyright (C) 2008-2009 David McCullough <david_mccullough@securecomputing.com>
 * Copyright (C) 2010,2012 Avesh Agarwal <avagarwa@redhat.com>
 * Copyright (C) 2010 Tuomo Soini <tis@foobar.fi
 * Copyright (C) 2012 Paul Wouters <pwouters@redhat.com>
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
 *
 */


/* This file is #include'ed into ikev2_parent.c */

/*
 *
 ***************************************************************
 *                       PARENT_inR1                       *****
 ***************************************************************
 *  -
 *
 *
 */
static void ikev2_parent_inR1outI2_continue(struct pluto_crypto_req_cont *pcrc
                                            , struct pluto_crypto_req *r
                                            , err_t ugh);

static stf_status
ikev2_parent_inR1outI2_tail(struct pluto_crypto_req_cont *pcrc
                            , struct pluto_crypto_req *r);



stf_status ikev2parent_inR1outI2(struct msg_digest *md)
{
    struct state *st = md->st;
    /* struct connection *c = st->st_connection; */
    pb_stream *keyex_pbs;

    /* record IKE version numbers -- used mostly in logging */
    st->st_ike_maj        = md->maj;
    st->st_ike_min        = md->min;

/*记录本地隧道接口的IP和端口*/
    if(isanyaddr(&st->st_localaddr) || st->st_localport == 0) {
        /* record where packet arrived to */
        st->st_localaddr  = md->iface->ip_addr;
        st->st_localport  = md->iface->port;
    }

    /*
     * verify the NAT DETECTION notify messages before answering.
     * on the responder side, this allows us to detect when *we* are behind
     * at NAPT (probably with a port-forward).
     *
     * If we are, then we set a bit saying so, which later on will make us pick the
     * UDP encapsulation for packets.  It is up to the initiator to switch ports
     * from 500 to 4500.  Could be they have already done so, we do not care here.
     */
    if(md->chain[ISAKMP_NEXT_v2N]) {/*解析报文中的通知载荷*/
        ikev2_process_notifies(st, md);

        /* switch to port 4500, if necessary */
        ikev2_update_nat_ports(st);/*如果存在NAT,则进行端口浮动*/
    }


    /* check if the responder replied with v2N with DOS COOKIE */
    /*
	* 如果响应端对发起端进行cookie challenge抗攻击检测，�
	* 则应答报文中会包含类型为COOKIE的通知载荷
	* 这时应该将dos cookie存储到状态上后重新发起协商
	*
	*---Challenge载荷应该与NATT载荷不会同时出现，否则获取到的v2N载荷有问题
	*/
    if( md->chain[ISAKMP_NEXT_v2N]
        && md->chain[ISAKMP_NEXT_v2N]->payload.v2n.isan_type ==  v2N_COOKIE)
        {
            u_int8_t spisize;
            const pb_stream *dc_pbs;
            DBG(DBG_CONTROLMORE
                ,DBG_log("inR1OutI2 received a DOS v2N_COOKIE from the responder");
                DBG_log("resend the I1 with a cookie payload"));
            spisize = md->chain[ISAKMP_NEXT_v2N]->payload.v2n.isan_spisize;
            dc_pbs = &md->chain[ISAKMP_NEXT_v2N]->pbs;
		/*将dcookie存放到状态上，重新发起协商*/
            clonetochunk(st->st_dcookie,  (dc_pbs->cur + spisize)
                         , (pbs_left(dc_pbs) - spisize), "saved received dcookie");

            DBG(DBG_CONTROLMORE
                ,DBG_dump_chunk("dcookie received (instead of a R1):",
                                st->st_dcookie);
                DBG_log("next STATE_PARENT_I1 resend I1 with the dcookie"));

            md->svm = &ikev2_parent_firststate_microcode;

            /* now reset state, and try again with noncense */
            change_state(st, STATE_PARENT_I1);
            st->st_msgid_lastack = INVALID_MSGID;
            md->msgid_received = INVALID_MSGID;  /* AAA hack  */
            st->st_msgid_nextuse = 0;

		/*重新发起协商*/
            return ikev2_parent_outI1_common(md, st);
        }

    /*
     * the responder sent us back KE, Gr, Nr, and it's our time to calculate
     * the shared key values.
     */

    DBG(DBG_CONTROLMORE
        , DBG_log("ikev2 parent inR1: calculating g^{xy} in order to send I2"));

    /* KE in *//*解析KE载荷*/
    keyex_pbs = &md->chain[ISAKMP_NEXT_v2KE]->pbs;
    RETURN_STF_FAILURE(accept_KE(&st->st_gr, "Gr", st->st_oakley.group, keyex_pbs));

    /* Ni in *//*解析Nonce载荷*/
    RETURN_STF_FAILURE(accept_v2_nonce(md, &st->st_nr, "Ni"));

    if(md->chain[ISAKMP_NEXT_v2SA] == NULL) {
        openswan_log("No responder SA proposal found");
        return PAYLOAD_MALFORMED;
    }

    /* process and confirm the SA selected */
    {
        struct payload_digest *const sa_pd = md->chain[ISAKMP_NEXT_v2SA];
        v2_notification_t rn;

        /* SA body in and out *//*解析对端选中的SA载荷*/
        rn = ikev2_parse_parent_sa_body(&sa_pd->pbs, &sa_pd->payload.v2sa,
                                        NULL, st, FALSE);

        if (rn != v2N_NOTHING_WRONG)
            return STF_FAIL + rn;
    }

    /* update state *//*更新状态和msgid序号*/
    ikev2_update_counters(md);

    /* now. we need to go calculate the g^xy */
    {/*通过DH算法计算密钥信息*/
        struct dh_continuation *dh = alloc_thing(struct dh_continuation
                                                 , "ikev2_inR1outI2 KE");
        stf_status e;

        dh->md = md;
        set_suspended(st, dh->md);

        pcrc_init(&dh->dh_pcrc);
        dh->dh_pcrc.pcrc_func = ikev2_parent_inR1outI2_continue;
	/*计算密钥信息*/
        e = start_dh_v2(&dh->dh_pcrc, st, st->st_import, INITIATOR, st->st_oakley.groupnum);
        if(e != STF_SUSPEND && e != STF_INLINE) {
            loglog(RC_CRYPTOFAILED, "system too busy");
            delete_state(st);
        }

        reset_globals();

        return e;
    }
}

static void
ikev2_parent_inR1outI2_continue(struct pluto_crypto_req_cont *pcrc
                                , struct pluto_crypto_req *r
                                , err_t ugh)
{
    struct dh_continuation *dh = (struct dh_continuation *)pcrc;
    struct msg_digest *md = dh->md;
    struct state *const st = md->st;
    stf_status e;

    DBG(DBG_CONTROLMORE
        , DBG_log("ikev2 parent inR1outI2: calculating g^{xy}, sending I2"));

    if (st == NULL) {
        loglog(RC_LOG_SERIOUS, "%s: Request was disconnected from state",
               __FUNCTION__);
        if (dh->md)
            release_md(dh->md);
        return;
    }

    /* XXX should check out ugh */
    passert(ugh == NULL);
    passert(cur_state == NULL);
    passert(st != NULL);

    passert(st->st_suspended_md == dh->md);
    set_suspended(st,NULL);        /* no longer connected or suspended */

    set_cur_state(st);

    st->st_calculating = FALSE;

    e = ikev2_parent_inR1outI2_tail(pcrc, r);

    if(dh->md != NULL) {
        complete_v2_state_transition(&dh->md, e);
        if(dh->md) release_md(dh->md);
    }
    reset_globals();

    passert(GLOBALS_ARE_RESET());
}

static stf_status
ikev2_parent_inR1outI2_tail(struct pluto_crypto_req_cont *pcrc
                            , struct pluto_crypto_req *r)
{
    struct dh_continuation *dh = (struct dh_continuation *)pcrc;
    struct msg_digest *md = dh->md;
    struct state *st      = md->st;
    struct connection *c  = st->st_connection;
    struct ikev2_generic e;
    unsigned char *encstart;
    pb_stream      e_pbs, e_pbs_cipher;
    unsigned char *iv;
    int            ivsize;
    stf_status     ret;
    unsigned char *idhash;
    unsigned char *authstart;
    struct state *pst = st;
    msgid_t        mid = INVALID_MSGID;

    md->transition_state = st;

    finish_dh_v2(st, r);/*非常重要的函数，将生成的密钥存储在state上*/

    if(DBGP(DBG_PRIVATE) && DBGP(DBG_CRYPT)) {
        ikev2_log_parentSA(st);
	ikev2_validate_key_lengths(st);
    }

    pst = st;
    ret = allocate_msgid_from_parent(pst, &mid);/*分配msgid， 类似于TCP中的序号*/
    if(ret != STF_OK) {
        /*
         * XXX: need to return here, having enqueued our pluto_crypto_req_cont
         * onto a structure on the parent for processing when there is message
         * ID available.
         */
        return ret;
    }

    /* okay, got a transmit slot, make a child state to send this. */
	/*创建一个child state*/
    st = duplicate_state(pst);
    st->st_policy = pst->st_connection->policy & POLICY_IPSEC_MASK;

    st->st_msgid = mid;
    insert_state(st);
    md->st = st;
    md->pst= pst;

    /* parent had crypto failed, replace it with rekey! */
    delete_event(pst);
    event_schedule(EVENT_SA_REPLACE, c->sa_ike_life_seconds, pst);

    /* record first packet for later checking of signature */
	/*记录下对端第一包*/
    clonetochunk(pst->st_firstpacket_him, md->message_pbs.start
                 , pbs_offset(&md->message_pbs), "saved first received packet");

    /* beginning of data going out */
    authstart = reply_stream.cur;/*从头开始认证*/

    /* make sure HDR is at start of a clean buffer */
    zero(reply_buffer);
    init_pbs(&reply_stream, reply_buffer, sizeof(reply_buffer), "reply packet");

    /* HDR out */
    {
        struct isakmp_hdr r_hdr = md->hdr;

        /* should be set to version received */
        // r_hdr.isa_version = IKEv2_MAJOR_VERSION << ISA_MAJ_SHIFT | IKEv2_MINOR_VERSION;
        r_hdr.isa_np    = ISAKMP_NEXT_v2E;
        r_hdr.isa_xchg  = ISAKMP_v2_AUTH;
        r_hdr.isa_flags = IKEv2_ORIG_INITIATOR_FLAG(pst);
        r_hdr.isa_msgid = htonl(st->st_msgid);
        memcpy(r_hdr.isa_icookie, st->st_icookie, COOKIE_SIZE);
        memcpy(r_hdr.isa_rcookie, st->st_rcookie, COOKIE_SIZE);
        if (!out_struct(&r_hdr, &isakmp_hdr_desc, &reply_stream, &md->rbody))
            return STF_INTERNAL_ERROR;
    }

    /* insert an Encryption payload header */
    e.isag_np = ISAKMP_NEXT_v2IDi;/*IKEv2通用载荷头*/
    e.isag_critical = ISAKMP_PAYLOAD_NONCRITICAL;
    if(DBGP(IMPAIR_SEND_BOGUS_ISAKMP_FLAG)) {
        openswan_log(" setting bogus ISAKMP_PAYLOAD_OPENSWAN_BOGUS flag in ISAKMP payload");
        e.isag_critical |= ISAKMP_PAYLOAD_OPENSWAN_BOGUS;
    }

    if(!out_struct(&e, &ikev2_e_desc, &md->rbody, &e_pbs)) {/*后续载荷通过e_pbs来继续填充*/
        return STF_INTERNAL_ERROR;
    }

    /* insert IV *//**/
    {
	    iv     = e_pbs.cur;
	    ivsize = st->st_oakley.encrypter->iv_size;
	    if(!out_zero(ivsize, &e_pbs, "iv")) {
	        return STF_INTERNAL_ERROR;
	    }
	    get_rnd_bytes(iv, ivsize);/*随机生成初始化向量IV，填充到报文中*/
    }
    /* note where cleartext starts */
    init_sub_pbs(&e_pbs, &e_pbs_cipher, "cleartext");/*加密部分使用e_pbs_cipher来填充*/
    encstart = e_pbs_cipher.cur;

    /* send out the IDi payload */
    {
        struct ikev2_id r_id;
        pb_stream r_id_pbs;
        chunk_t         id_b;
        struct hmac_ctx id_ctx;

        /* for calculation of hash of ID payload */
        unsigned char *id_start;
        unsigned int   id_len;
	/*构造ID载荷*/
        build_id_payload((struct isakmp_ipsec_id *)&r_id, &id_b, &c->spd.this);
        r_id.isai_critical = ISAKMP_PAYLOAD_NONCRITICAL;
        if(DBGP(IMPAIR_SEND_BOGUS_ISAKMP_FLAG)) {
            openswan_log(" setting bogus ISAKMP_PAYLOAD_OPENSWAN_BOGUS flag in ISAKMP payload");
            r_id.isai_critical |= ISAKMP_PAYLOAD_OPENSWAN_BOGUS;
        }

        r_id.isai_np = 0;

        pbs_set_np(&e_pbs_cipher, ISAKMP_NEXT_v2IDi);/*设置上一个载荷的NP字段*/
        id_start = e_pbs_cipher.cur;
        if (!out_struct(&r_id /*填充ID载荷头部信息: id类型，如ipv4,fqdn,...*/
                        , &ikev2_id_desc
                        , &e_pbs_cipher
                        , &r_id_pbs)) {
            return STF_INTERNAL_ERROR;
        }
	/*填充ID载荷数据信息*/
        if(!out_chunk(id_b, &r_id_pbs, "my identity")) {
            return STF_INTERNAL_ERROR;
        }

	{
	        /* HASH of ID is not done over common (NP/length) header */
	        id_start += 4;
	        id_len   = r_id_pbs.cur - id_start;

	        /* calculate hash of IDi for AUTH below */
	        hmac_init_chunk(&id_ctx, pst->st_oakley.prf_hasher, pst->st_skey_pi);
	        hmac_update(&id_ctx, id_start, id_len);
	        idhash = alloca(pst->st_oakley.prf_hasher->hash_digest_len);
	        hmac_final(idhash, &id_ctx);/*下面的认证载荷用到该值*/
	}


        close_output_pbs(&r_id_pbs);
    }

    /* send [CERT,] payload RFC 4306 3.6, 1.2) */
	/*
	*	1. 配置中采用证书认证
	*	2. 已经导入证书
	*	3. 本端强制要求发送证书
	*	4. 对端请求本端发送证书
	*/
    if(doi_send_ikev2_cert_thinking(st)) {/*是否需要发送证书类载荷: CERT, CERTREQ*/
        stf_status certstat = ikev2_send_cert( st, md
                                               , INITIATOR
                                               , ISAKMP_NEXT_v2AUTH
                                               , &e_pbs_cipher);
        if(certstat != STF_OK) return certstat;
    }

    /* send out the AUTH payload *//*报文中有一个单独的认证载荷*/
    {
        lset_t policy;
        struct connection *c0= first_pending(pst, &policy,&st->st_whack_sock);
        unsigned int np = (c0 ? ISAKMP_NEXT_v2SA : ISAKMP_NEXT_NONE);
        DBG(DBG_CONTROL,DBG_log(" payload after AUTH will be %s", (c0) ? "ISAKMP_NEXT_v2SA" : "ISAKMP_NEXT_NONE/NOTIFY"));

	/*填充认证载荷数据(不同于报文最后的完整性载荷)
	*
	*		PRF、presharekey、idhash、nonce、firstpacket
	**/
	
        stf_status authstat = ikev2_send_auth(c, st
                                              , INITIATOR
                                              , np
                                              , idhash, &e_pbs_cipher);

	
        if(authstat != STF_OK) return authstat;

        /*
         * now, find an eligible child SA from the pending list, and emit
         * SA2i, TSi and TSr and (v2N_USE_TRANSPORT_MODE notification in transport mode) for it .
         */
        if(c0) {
            chunk_t child_spi, notify_data;
            unsigned int next_payload = ISAKMP_NEXT_NONE;
            st->st_connection = c0;

            if( !(st->st_connection->policy & POLICY_TUNNEL) ) {
                next_payload = ISAKMP_NEXT_v2N;
            }

		/*发送第二阶段IPsecSA*/
	    ikev2_emit_ipsec_sa(md,&e_pbs_cipher,ISAKMP_NEXT_v2TSi,c0, policy);

		/*将本端和对端保护子网转换为ts结构，填充到st上*/
	    st->st_ts_this = ikev2_end_to_ts(&c0->spd.this, st->st_localaddr);
	    st->st_ts_that = ikev2_end_to_ts(&c0->spd.that, st->st_remoteaddr);

		/*填充本端和对端TS载荷*/
	    ikev2_calc_emit_ts(md, &e_pbs_cipher, INITIATOR, next_payload, c0, policy);

            if( !(st->st_connection->policy & POLICY_TUNNEL) ) {/*传输模式*/
                DBG_log("Initiator child policy is transport mode, sending v2N_USE_TRANSPORT_MODE");
                memset(&child_spi, 0, sizeof(child_spi));
                memset(&notify_data, 0, sizeof(notify_data));
                ship_v2N (ISAKMP_NEXT_NONE, ISAKMP_PAYLOAD_NONCRITICAL, 0,
                          &child_spi,
                          v2N_USE_TRANSPORT_MODE, &notify_data, &e_pbs_cipher);
            }

            /* need to force child to KEYING */
            change_state(st, STATE_CHILD_C0_KEYING);
        } else {
            openswan_log("no pending SAs found, PARENT SA keyed only");
        }
    }

    /*
     * need to extend the packet so that we will know how big it is
     * since the length is under the integrity check
     */
    ikev2_padup_pre_encrypt(md, &e_pbs_cipher);
    close_output_pbs(&e_pbs_cipher);

    {
        unsigned char *authloc = ikev2_authloc(md, &e_pbs);

        if(authloc == NULL || authloc < encstart) return STF_INTERNAL_ERROR;

        close_output_pbs(&e_pbs);
        close_output_pbs(&md->rbody);
        close_output_pbs(&reply_stream);

	/*对报文进行加密和完整性hash计算*/
        ret = ikev2_encrypt_msg(md, INITIATOR,
                                authstart,
                                iv, encstart, authloc,
                                &e_pbs, &e_pbs_cipher);
        if(ret != STF_OK) return ret;
    }


    /* let TCL hack it before we mark the length. */
    TCLCALLOUT("v2_avoidEmitting", st, st->st_connection, md);

    /* keep it for a retransmit if necessary, but on initiator
     * we never do that, but send_packet() uses it.
     */
    freeanychunk(pst->st_tpacket);
    clonetochunk(pst->st_tpacket, reply_stream.start, pbs_offset(&reply_stream)
                 , "reply packet for ikev2_parent_outI1");

    /*
     * Delete previous retransmission event.
     */
    delete_event(st);
    event_schedule(EVENT_v2_RETRANSMIT, EVENT_RETRANSMIT_DELAY_0, st);

    return STF_OK;

}

/*
 * this routine deals with replies that are failures, which do not
 * contain proposal, or which require us to try initiator cookies.
 */
stf_status ikev2parent_inR1(struct msg_digest *md)
{
    struct state *st = md->st;
    /* struct connection *c = st->st_connection; */

    set_cur_state(st);

    /* check if the responder replied with v2N with DOS COOKIE */
    if( md->chain[ISAKMP_NEXT_v2N] ) {/*首先解析报文中的通知载荷*/
        struct payload_digest *notify;
        const char *action = "ignored";

	/*遍历所有的建议载荷*/
        for(notify=md->chain[ISAKMP_NEXT_v2N]; notify!=NULL; notify=notify->next) {
            switch(notify->payload.v2n.isan_type) {
            case v2N_NO_PROPOSAL_CHOSEN:/*建议载荷不合适*/
                action="SA deleted";
                break;
            case v2N_INVALID_KE_PAYLOAD:/*DH组猜错情况下会返回此提示信息*/
                action="SA deleted";
                break;
            default:
                break;
            }

            loglog(RC_NOTIFICATION + notify->payload.v2n.isan_type
                      , "received notify: %s %s"
                      ,enum_name(&ikev2_notify_names
                                 , notify->payload.v2n.isan_type)
                      ,action);
        }

    }

    /* now. nuke the state */
    {
        delete_state(st);
        reset_globals();
        return STF_FAIL;
    }
}

/*
 * Local Variables:
 * c-basic-offset:4
 * c-style: pluto
 * End:
 */
