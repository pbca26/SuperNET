/******************************************************************************
 * Copyright © 2014-2017 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

// included from basilisk.c

// "currency":{"value":%.8f, "pending":%.8f}

cJSON *tradebot_balancediff(cJSON *item,cJSON *anchoritem) // only item might be null
{
    double current[2],past[2]; int32_t i; cJSON *diffitem = jduplicate(anchoritem);
    memset(current,0,sizeof(current));
    memset(past,0,sizeof(past));
    if ( jobj(diffitem,"value") != 0 )
        jdelete(diffitem,"value");
    if ( jobj(diffitem,"pending") != 0 )
        jdelete(diffitem,"pending");
    for (i=0; i<2; i++)
    {
        if ( current[i] != 0. || past[i] != 0. )
            jaddnum(diffitem,i == 0 ? "value" : "pending",current[i] - past[i]);
    }
    return(diffitem);
}

cJSON *tradebot_balancesdiff(struct supernet_info *myinfo,cJSON *current,cJSON *anchor)
{
    cJSON *item,*anchoritem,*diffitem,*array; int32_t i,n; char *field;
    if ( anchor == 0 )
        return(jduplicate(current));
    array = cJSON_CreateObject();
    n = cJSON_GetArraySize(current);
    for (i=0; i<n; i++)
    {
        item = jitem(current,i);
        field = jfieldname(item);
        if ( (anchoritem= jobj(anchor,field)) != 0 )
            diffitem = tradebot_balancediff(item,anchoritem);
        else diffitem = jduplicate(item);
        jadd(array,field,diffitem);
    }
    n = cJSON_GetArraySize(anchor);
    for (i=0; i<n; i++)
    {
        item = jitem(current,i);
        field = jfieldname(item);
        if ( jobj(array,field) == 0 )
            jadd(array,field,tradebot_balancediff(0,item));
    }
    return(array);
}

// get balances from all exchanges, wallets, pending
double tradebot_balance(struct supernet_info *myinfo,char *base)
{
    cJSON *json; double value = 0.; int32_t i; struct iguana_info *coin = iguana_coinfind(base);
    if ( coin != 0 && (json= iguana_getinfo(myinfo,coin)) != 0 )
    {
        value = jdouble(json,"balance");
        free_json(json);
    }
    for (i=0; i<myinfo->numexchanges; i++)
    {
        value += 0;//InstantDEX_balance(myinfo,0,0,0,exchange,base);
    }
    return(value);
}

void tradebot_pendingadd(struct supernet_info *myinfo,cJSON *tradejson,char *base,double basevolume,char *rel,double relvolume)
{
    portable_mutex_lock(&myinfo->pending_mutex);
    // add to myinfo->trades
    portable_mutex_unlock(&myinfo->pending_mutex);
}

void tradebot_pendingremove(struct supernet_info *myinfo,char *base,double basevolume,char *rel,double relvolume)
{
    portable_mutex_lock(&myinfo->pending_mutex);
    // remove from myinfo->trades
    portable_mutex_unlock(&myinfo->pending_mutex);
}

double tradebot_pending(struct supernet_info *myinfo,char *base)
{
    double pending = 0.; struct pending_trade *tp,*tmp;
    portable_mutex_lock(&myinfo->pending_mutex);
    HASH_ITER(hh,myinfo->trades,tp,tmp)
    {
        if ( strcmp(base,tp->base) == 0 )
            pending += tp->dir * tp->basevolume;
        else if ( strcmp(base,tp->rel) == 0 )
            pending -= tp->dir * tp->relvolume;
    }
    portable_mutex_unlock(&myinfo->pending_mutex);
    return(pending);
}

#include "../includes/iguana_apidefs.h"
#include "../includes/iguana_apideclares.h"

ZERO_ARGS(tradebot,allbalances)
{
    int32_t i,n; double value,pending; char *base; cJSON *item,*balances = cJSON_CreateObject();
    if ( myinfo->liquidity_currencies == 0 )
        myinfo->liquidity_currencies = cJSON_Parse("[\"KMD\", \"BTC\"]");
    if ( myinfo->liquidity_currencies != 0 && (n= cJSON_GetArraySize(myinfo->liquidity_currencies)) > 0 )
    {
        for (i=0; i<n; i++)
        {
            base = jstri(myinfo->liquidity_currencies,i);
            value = tradebot_balance(myinfo,base);
            pending = tradebot_pending(myinfo,base);
            item = cJSON_CreateObject();
            jaddnum(item,"value",value);
            jaddnum(item,"pending",pending);
            jadd(balances,base,item);
        }
    }
    return(jprint(balances,1));
}

ZERO_ARGS(tradebot,anchor)
{
    FILE *fp; char *anchorstr,fname[512]; cJSON *anchor; int32_t retval = -1;
    if ( (anchorstr= tradebot_allbalances(myinfo,0,0,0)) != 0 )
    {
        if ( (anchor= cJSON_Parse(anchorstr)) != 0 )
        {
            if ( jobj(anchor,"error") == 0 )
            {
                sprintf(fname,"%s/anchor",GLOBAL_DBDIR), OS_compatible_path(fname);
                if ( (fp= fopen(fname,"wb")) != 0 )
                {
                    if ( fwrite(anchorstr,1,strlen(anchorstr)+1,fp) == strlen(anchorstr)+1 )
                        retval = 0;
                    fclose(fp);
                }
            }
        }
        free(anchorstr);
    }
    if ( retval == 0 )
        return(clonestr("{\"result\":\"success\"}"));
    else return(clonestr("{\"error\":\"couldnt make anchor file\"}"));
}

ZERO_ARGS(tradebot,portfolio)
{
    char *currentstr,*anchorstr,fname[512]; long fsize; cJSON *current,*anchor=0,*portfolio=0;
    if ( (currentstr= tradebot_allbalances(myinfo,0,0,0)) != 0 )
    {
        if ( (current= cJSON_Parse(currentstr)) != 0 )
        {
            sprintf(fname,"%s/anchor",GLOBAL_DBDIR), OS_compatible_path(fname);
            if ( (anchorstr= OS_filestr(&fsize,fname)) != 0 )
            {
                anchor = cJSON_Parse(anchorstr);
                free(anchorstr);
            }
            if ( anchor == 0 )
                anchor = cJSON_Parse("{}");
            portfolio = tradebot_balancesdiff(myinfo,current,anchor);
            free_json(current);
        }
        free(currentstr);
    }
    if ( portfolio == 0 )
        return(clonestr("{\"result\":\"success\"}"));
    else return(jprint(portfolio,1));
}

/*li.profit = jdouble(vals,"profit");
li.refprice = jdouble(vals,"refprice");
li.bid = jdouble(vals,"bid");
li.ask = jdouble(vals,"ask");
if ( (li.minvol= jdouble(vals,"minvol")) <= 0. )
li.minvol = (strcmp("BTC",base) == 0) ? 0.0001 : 0.001;
if ( (li.maxvol= jdouble(vals,"maxvol")) < li.minvol )
li.maxvol = li.minvol;*/

ARRAY_OBJ_INT(tradebot,goals,currencies,vals,targettime)
{
    static bits256 zero; char *targetcoin; int32_t i,n;
    if ( currencies != 0 && vals != 0 )
    {
    // init things so automatically updates refli.bid and refli.ask
    // volume range with margin
    // currency percentage value in BTC? target distribution, max percentage, min percentage`
    // min price to sell, max price to buy, max volume
        n = cJSON_GetArraySize(currencies);
        for (i=0; i<n; i++)
        {
            targetcoin = jstri(currencies,i);
            tradebot_liquidity_command(myinfo,targetcoin,zero,vals);
        }
    return(clonestr("{\"result\":\"success\"}"));
    } else return(clonestr("{\"error\":\"no currencies or vals\"}"));
}

#include "../includes/iguana_apiundefs.h"
