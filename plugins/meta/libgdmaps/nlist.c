/* Copyright © 2012 Brandon L Black <blblack@gmail.com>
 *
 * This file is part of gdnsd-plugin-geoip.
 *
 * gdnsd-plugin-geoip is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gdnsd-plugin-geoip is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gdnsd.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"
#include "nlist.h"
#include <string.h>
#include <stdlib.h>
#include <gdnsd-log.h>
#include <gdnsd-misc.h>

#define NLIST_INITSIZE 64
#define MASK_DELETED 0xFFFFFFFF

typedef struct {
    uint8_t ipv6[16];
    unsigned mask;
    unsigned dclist;
} net_t;

struct _nlist {
    net_t* nets;
    char* map_name;
    unsigned alloc;
    unsigned count;
    bool pre_norm;
    bool finished;
};

nlist_t* nlist_new(const char* map_name, const bool pre_norm) {
    dmn_assert(map_name);
    nlist_t* nl = malloc(sizeof(nlist_t));
    nl->nets = malloc(sizeof(net_t) * NLIST_INITSIZE);
    nl->map_name = strdup(map_name);
    nl->alloc = NLIST_INITSIZE;
    nl->count = 0;
    nl->pre_norm = pre_norm;
    nl->finished = false;
    return nl;
}

// only used for normalization assertions in debug builds...
F_UNUSED F_NONNULL
static nlist_t* nlist_clone(const nlist_t* nl) {
    dmn_assert(nl);
    nlist_t* nlc = malloc(sizeof(nlist_t));
    nlc->map_name = strdup(nl->map_name);
    nlc->alloc = nl->alloc;
    nlc->count = nl->count;
    nlc->pre_norm = nl->pre_norm;
    nlc->finished = nl->finished;
    nlc->nets = malloc(sizeof(net_t) * nlc->alloc);
    memcpy(nlc->nets, nl->nets, sizeof(net_t) * nlc->count);
    return nlc;
}

void nlist_debug_dump(const nlist_t* nl) {
    dmn_assert(nl);
    log_debug(" --- nlist debug on %s --- ", nl->map_name);
    for(unsigned i = 0; i < nl->count; i++)
        log_debug("   %s/%u -> %u", logf_ipv6(nl->nets[i].ipv6), nl->nets[i].mask, nl->nets[i].dclist);
}

void nlist_destroy(nlist_t* nl) {
    dmn_assert(nl);
    free(nl->map_name);
    free(nl->nets);
    free(nl);
}

#ifndef NDEBUG
F_NONNULL
static void assert_clear_mask_bits(uint8_t* ipv6, const unsigned mask) {
    dmn_assert(ipv6); dmn_assert(mask < 129);

    if(likely(mask)) {
        const unsigned revmask = 128 - mask;
        const unsigned byte_mask = ~(0xFF << (revmask & 7)) & 0xFF;
        unsigned bbyte = 15 - (revmask >> 3);

        dmn_assert(!(ipv6[bbyte] & byte_mask));
        while(++bbyte < 16)
            dmn_assert(!ipv6[bbyte]);
    }
    else {
        dmn_assert(!memcmp(ipv6, &ip6_zero, 16));
    }
}
#else
#define assert_clear_mask_bits(x, y)
#endif

F_NONNULL
static void clear_mask_bits(const char* map_name, uint8_t* ipv6, const unsigned mask) {
    dmn_assert(map_name); dmn_assert(ipv6); dmn_assert(mask < 129);

    bool maskbad = false;

    if(likely(mask)) {
        const unsigned revmask = 128 - mask;
        const unsigned byte_mask = ~(0xFF << (revmask & 7)) & 0xFF;
        unsigned bbyte = 15 - (revmask >> 3);

        if(ipv6[bbyte] & byte_mask) {
            maskbad = true;
            ipv6[bbyte] &= ~byte_mask;
        }

        while(++bbyte < 16) {
            if(ipv6[bbyte]) {
                maskbad = true;
                ipv6[bbyte] = 0;
            }
        }
    }
    else if(memcmp(ipv6, &ip6_zero, 16)) {
        maskbad = true;
        memset(ipv6, 0, 16);
    }

    if(maskbad)
        log_warn("plugin_geoip: map '%s': input network %s/%u had illegal bits beyond mask, which were cleared", map_name, logf_ipv6(ipv6), mask);
}

// Sort an array of net_t.  Sort prefers
//   lowest network number, smallest mask.
F_NONNULL F_PURE
static int net_sorter(const void* a_void, const void* b_void) {
    dmn_assert(a_void); dmn_assert(b_void);
    const net_t* a = (const net_t*)a_void;
    const net_t* b = (const net_t*)b_void;
    int rv = memcmp(a->ipv6, b->ipv6, 16);
    if(!rv)
        rv = a->mask - b->mask;
    return rv;
}

// sort and check for dupes
F_NONNULL
static bool nlist_normalize1(nlist_t* nl) {
    dmn_assert(nl);

    if(nl->count) {
        qsort(nl->nets, nl->count, sizeof(net_t), net_sorter);

        for(unsigned i = 0; i < (nl->count - 1); i++) {
            net_t* net_a = &nl->nets[i];
            net_t* net_b = &nl->nets[i + 1];
            if(net_a->mask == net_b->mask && !memcmp(net_a->ipv6, net_b->ipv6, 16)) {
               // technically, we could allow this to succeed if dclists are identical,
               //  but we'd have to delete one and rewrite the list, as we do during n2...
               log_err("plugin_geoip: map '%s': duplicate network %s/%u",
                   nl->map_name, logf_ipv6(net_a->ipv6), net_a->mask);
               return true;
            }
        }
    }

    return false;
}

F_NONNULL F_PURE
static bool masked_net_eq(const uint8_t* v6a, const uint8_t* v6b, const unsigned mask) {
    dmn_assert(v6a); dmn_assert(v6b);
    dmn_assert(mask < 128U); // 2x128 would call here w/ 127...

    const unsigned bytes = mask >> 3;
    dmn_assert(bytes < 16U);

    const unsigned bytemask = (0xFF00 >> (mask & 7)) & 0xFF;
    return !memcmp(v6a, v6b, bytes)
        && (v6a[bytes] & bytemask) == (v6b[bytes] & bytemask);
}

F_NONNULL F_PURE
static bool mergeable_nets(const net_t* na, const net_t* nb) {
    dmn_assert(na); dmn_assert(nb);
    return (na->mask == nb->mask
        && na->dclist == nb->dclist
        && masked_net_eq(na->ipv6, nb->ipv6, na->mask - 1));
}

void nlist_append(nlist_t* nl, const uint8_t* ipv6, const unsigned mask, const unsigned dclist) {
    dmn_assert(nl); dmn_assert(ipv6);

    if(unlikely(nl->count == nl->alloc)) {
        nl->alloc <<= 1U;
        nl->nets = realloc(nl->nets, sizeof(net_t) * nl->alloc);
    }
    net_t* this_net = &nl->nets[nl->count++];
    memcpy(this_net->ipv6, ipv6, 16U);
    this_net->mask = mask;
    this_net->dclist = dclist;

    // In the pre-norm case, we can keep the list in fully-normalized
    //   form as we go by doing this merge op after each append and
    //   keeping the list minimized.  What we're catching here is adjacent
    //   networks which share a dclist and mask and are thus mergeable,
    //   and we do so by deleting the most-recently added one and decrementing
    //   the subnet mask of the older one.
    // Because this is happening back-to-front after each append, there's
    //   no need to create (or later deal with) holes in the array as we
    //   do during normalize2() for unsorted inputs.
    if(nl->pre_norm) {
        assert_clear_mask_bits(this_net->ipv6, mask);
        unsigned idx = nl->count;
        while(--idx > 0) {
            net_t* nb = &nl->nets[idx];
            net_t* na = &nl->nets[idx - 1];
            if(mergeable_nets(na, nb)) {
                na->mask--;
                nl->count--;
            }
            else {
                break;
            }
        }
    }
    // for raw input, just correct any netmask errors as we insert,
    //   as these will screw up later sorting for normalize1
    else {
        clear_mask_bits(nl->map_name, this_net->ipv6, mask);
    }
}

// merge adjacent nets with identical dclists recursively...
F_NONNULL
static void nlist_normalize2(nlist_t* nl) {
    dmn_assert(nl);

    if(nl->count) {
        unsigned idx = nl->count;
        unsigned newcount = nl->count;
        while(--idx > 0) {
            net_t* nb = &nl->nets[idx];
            net_t* na = &nl->nets[idx - 1];
            if(mergeable_nets(na, nb)) {
                // na should have the differential bit clear
                //   thanks to pre-sorting, so just needs mask--
                nb->mask = MASK_DELETED;
                na->mask--;
                newcount--;
                unsigned upidx = idx + 1;
                while(upidx < nl->count) {
                    net_t* nc = &nl->nets[upidx];
                    if(nc->mask != MASK_DELETED) {
                        if(mergeable_nets(na, nc)) {
                            nc->mask = MASK_DELETED;
                            na->mask--;
                            newcount--;
                        }
                        else {
                            break;
                        }
                    }
                    upidx++;
                }
            }
        }

        if(newcount != nl->count) { // merges happened
            net_t* newnets = malloc(sizeof(net_t) * newcount);
            unsigned newidx = 0;
            for(unsigned i = 0; i < nl->count; i++) {
                net_t* n = &nl->nets[i];
                if(n->mask != MASK_DELETED)
                    memcpy(&newnets[newidx++], n, sizeof(net_t));
            }
            dmn_assert(newidx == newcount);
            free(nl->nets);
            nl->nets = newnets;
            nl->count = newcount;
            nl->alloc = newcount;
        }
        else { // just optimize nets size
            nl->alloc = nl->count;
            nl->nets = realloc(nl->nets, sizeof(net_t) * nl->alloc);
        }
    }
}

F_NONNULL
bool nlist_finish(nlist_t* nl) {
    dmn_assert(nl);

    bool rv = false;
    if(nl->pre_norm) {
#ifndef NDEBUG
        // assert normalization in debug builds via clone->normalize->compare
        nlist_t* nlc = nlist_clone(nl);
        dmn_assert(!nlist_normalize1(nlc));
        nlist_normalize2(nlc);
        dmn_assert(nlc->count == nl->count);
        dmn_assert(!memcmp(nlc->nets, nl->nets, sizeof(net_t) * nlc->count));
        nlist_destroy(nlc);
#endif
        nl->finished = true;
    }
    else {
        rv = nlist_normalize1(nl);
        if(!rv) {
            nlist_normalize2(nl);
            nl->finished = true;
        }
    }

    return rv;
}

F_NONNULL F_PURE
static bool net_subnet_of(const net_t* sub, const net_t* super) {
    dmn_assert(sub); dmn_assert(super);
    dmn_assert(sub->mask < 129);
    dmn_assert(super->mask < 129);

    bool rv = false;
    if(sub->mask >= super->mask) {
        const unsigned wbyte = (super->mask >> 3);
        const unsigned byte_mask = (0xFF << (8 - (super->mask & 7))) & 0xFF;
        if(!memcmp(sub->ipv6, super->ipv6, wbyte)) {
            if(wbyte == 16 || (super->ipv6[wbyte] & byte_mask) == (sub->ipv6[wbyte] & byte_mask))
                rv = true;
        }
    }

    return rv;
}

F_NONNULL
static nlist_t* nlist_merge(const nlist_t* nl_a, const nlist_t* nl_b) {
    dmn_assert(nl_a); dmn_assert(nl_b);
    dmn_assert(nl_a->finished);
    dmn_assert(nl_b->finished);

    nlist_t* merged = nlist_new(nl_a->map_name, false);

    const net_t* n_a = &nl_a->nets[0];
    const net_t* n_b = &nl_b->nets[0];
    const net_t* end_a = &nl_a->nets[nl_a->count];
    const net_t* end_b = &nl_b->nets[nl_b->count];

    while(n_a < end_a && n_b < end_b) {
        if(net_sorter(n_a, n_b) < 0) {
            // n_a < n_b
            //   therefore n_a is a supernet of the next n_b,
            //   or an unrelated predecessor, copy it...
            nlist_append(merged, n_a->ipv6, n_a->mask, n_a->dclist);
            n_a++;
        }
        else { // n_a >= n_b
            nlist_append(merged, n_b->ipv6, n_b->mask, n_b->dclist);
            // this is where we skip networks from the first list
            //   that are effectively masked out by entries in the second
            while(n_a < end_a && net_subnet_of(n_a, n_b))
               n_a++;
            n_b++;
        }
    }

    // Usually only one of the lists will have remaining entries,
    //   which should be copyable.  Rarely, both will already be finished.
    while(n_b < end_b) {
        nlist_append(merged, n_b->ipv6, n_b->mask, n_b->dclist);
        n_b++;
    }
    while(n_a < end_a) {
        nlist_append(merged, n_a->ipv6, n_a->mask, n_a->dclist);
        n_a++;
    }

    // custom finisher for merges: assert n1, do actual n2 work
#ifndef NDEBUG
    nlist_t* mc = nlist_clone(merged);
    dmn_assert(!nlist_normalize1(mc));
    dmn_assert(mc->count == merged->count);
    dmn_assert(!memcmp(mc->nets, merged->nets, sizeof(net_t) * mc->count));
    nlist_destroy(mc);
#endif
    nlist_normalize2(merged);
    merged->finished = true;

    return merged;
}

F_NONNULL
static unsigned nxt_rec(const net_t** nl, const net_t* const nl_end, ntree_t* nt, net_t tree_net);

F_NONNULL
static void nxt_rec_dir(const net_t** nlp, const net_t* const nl_end, ntree_t* nt, net_t tree_net, const unsigned nt_idx, const bool direction) {
    dmn_assert(nlp); dmn_assert(nl_end); dmn_assert(nt);
    dmn_assert(tree_net.mask < 129 && tree_net.mask > 0);

    const net_t* nl = *nlp;
    unsigned cnode;

    // If items remain in the list, and the next list item
    //   is a subnet of (including exact match for) the current
    //   ntree node...
    if(nl < nl_end && net_subnet_of(nl, &tree_net)) {
        // exact match, consume...
        if(tree_net.mask == nl->mask && !memcmp(tree_net.ipv6, nl->ipv6, 16)) { // exact match on zero
            (*nlp)++; // consume *nlp and move to next
            // need to pre-check for a deeper subnet next in the list.
            // We use the consumed entry as the new default and keep recursing
            //   if deeper subnets exist.  If they don't, we assign and end recursion...
            const net_t* nl_next = *nlp;
            if(nl_next < nl_end && net_subnet_of(nl_next, nl)) {
                tree_net.dclist = nl->dclist;
                cnode = nxt_rec(nlp, nl_end, nt, tree_net);
            }
            else {
                cnode = NN_SET_DCLIST(nl->dclist);
            }
        }
        // Not an exact match, so just keep recursing towards such a match...
        else {
            cnode = nxt_rec(nlp, nl_end, nt, tree_net);
        }
    }
    // list item isn't a subnet of the current tree node, and due to our
    //   normalization that means there are no such list items remaining,
    //   so terminate the recursion with the current default dclist.
    else {
        cnode = NN_SET_DCLIST(tree_net.dclist);
    }

    // store direct or recursed result.  Note we have to wait until
    //   here to deref nt->store[nt_idx] because recursion could
    //   re-allocate nt->store[] during nxt_rec()'s ntree_add_node() call.
    if(direction)
        nt->store[nt_idx].one = cnode;
    else
        nt->store[nt_idx].zero = cnode;
}

F_NONNULL
static unsigned nxt_rec(const net_t** nl, const net_t* const nl_end, ntree_t* nt, net_t tree_net) {
    dmn_assert(nl); dmn_assert(nl_end); dmn_assert(nt);
    dmn_assert(tree_net.mask < 128);
    tree_net.mask++; // now mask for zero/one stubs

    const unsigned nt_idx = ntree_add_node(nt);
    nxt_rec_dir(nl, nl_end, nt, tree_net, nt_idx, false);
    SETBIT_v6(tree_net.ipv6, tree_net.mask - 1);
    nxt_rec_dir(nl, nl_end, nt, tree_net, nt_idx, true);

    // catch missed optimizations during final translation,
    //   which should be on the default dclist zero
    //   due to it being implicit for undefined nets in the list,
    //   and thus not merged with true list entries...
    unsigned rv;
    if(unlikely(nt->store[nt_idx].zero == nt->store[nt_idx].one) && likely(nt_idx > 0)) {
        dmn_assert(nt->store[nt_idx].zero == NN_SET_DCLIST(0));
        nt->count--; // delete the just-added node
        rv = NN_SET_DCLIST(0); // retval is now a dclist rather than a node...
    }
    else {
        rv = nt_idx;
    }
    return rv;
}

ntree_t* nlist_xlate_tree(const nlist_t* nl) {
    dmn_assert(nl);
    dmn_assert(nl->finished);

    ntree_t* nt = ntree_new();
    const net_t* nlnet = &nl->nets[0];
    const net_t* const nlnet_end = &nl->nets[nl->count];
    net_t tree_net = {
        .ipv6 = { 0 },
        .mask = 0,
        .dclist = 0
    };

    // Special-case: if a list entry for ::/0 exists, it will
    //   be first in the list, and it needs to be skipped
    //   over (with its dclist as the new default) before
    //   recursing (because ::/0 is the first node of the
    //   tree itself).
    if(nl->count && !nl->nets[0].mask) {
        tree_net.dclist = nl->nets[0].dclist;
        nlnet++;
    }

    // recursively build the tree from the list
    nxt_rec(&nlnet, nlnet_end, nt, tree_net);

    // assert that the whole list was consumed
    dmn_assert(nlnet == nlnet_end);

    // finalize the tree
    ntree_finish(nt);

    // make sure all our logic worked out sanely
    ntree_assert_optimal(nt);

    return nt;
}

ntree_t* nlist_merge2_tree(const nlist_t* nl_a, const nlist_t* nl_b) {
    dmn_assert(nl_a); dmn_assert(nl_b);

    nlist_t* merged = nlist_merge(nl_a, nl_b);
    ntree_t* rv = nlist_xlate_tree(merged);
    nlist_destroy(merged);
    return rv;
}

ntree_t* nlist_merge3_tree(const nlist_t* nl_a, const nlist_t* nl_b, const nlist_t* nl_c) {
    dmn_assert(nl_a); dmn_assert(nl_b); dmn_assert(nl_c);

    nlist_t* merge1 = nlist_merge(nl_a, nl_b);
    nlist_t* merge2 = nlist_merge(merge1, nl_c);
    nlist_destroy(merge1);
    ntree_t* rv = nlist_xlate_tree(merge2);
    nlist_destroy(merge2);
    return rv;
}