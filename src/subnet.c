/*
    subnet.c -- handle subnet lookups and lists
    Copyright (C) 2000-2006 Guus Sliepen <guus@tinc-vpn.org>,
                  2000-2005 Ivo Timmermans

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    $Id$
*/

#include "system.h"

#include "splay_tree.h"
#include "device.h"
#include "logger.h"
#include "net.h"
#include "netutl.h"
#include "node.h"
#include "process.h"
#include "subnet.h"
#include "utils.h"
#include "xalloc.h"

/* lists type of subnet */

splay_tree_t *subnet_tree;

/* Subnet comparison */

static int subnet_compare_mac(const subnet_t *a, const subnet_t *b)
{
	int result;

	result = memcmp(&a->net.mac.address, &b->net.mac.address, sizeof a->net.mac.address);

	if(result || !a->owner || !b->owner)
		return result;

	return strcmp(a->owner->name, b->owner->name);
}

static int subnet_compare_ipv4(const subnet_t *a, const subnet_t *b)
{
	int result;

	result = memcmp(&a->net.ipv4.address, &b->net.ipv4.address, sizeof a->net.ipv4.address);

	if(result)
		return result;

	result = a->net.ipv4.prefixlength - b->net.ipv4.prefixlength;

	if(result || !a->owner || !b->owner)
		return result;

	return strcmp(a->owner->name, b->owner->name);
}

static int subnet_compare_ipv6(const subnet_t *a, const subnet_t *b)
{
	int result;

	result = memcmp(&a->net.ipv6.address, &b->net.ipv6.address, sizeof a->net.ipv6.address);

	if(result)
		return result;

	result = a->net.ipv6.prefixlength - b->net.ipv6.prefixlength;

	if(result || !a->owner || !b->owner)
		return result;

	return strcmp(a->owner->name, b->owner->name);
}

int subnet_compare(const subnet_t *a, const subnet_t *b)
{
	int result;

	result = a->type - b->type;

	if(result)
		return result;

	switch (a->type) {
	case SUBNET_MAC:
		return subnet_compare_mac(a, b);
	case SUBNET_IPV4:
		return subnet_compare_ipv4(a, b);
	case SUBNET_IPV6:
		return subnet_compare_ipv6(a, b);
	default:
		logger(LOG_ERR, _("subnet_compare() was called with unknown subnet type %d, exitting!"),
			   a->type);
		cp_trace();
		exit(0);
	}

	return 0;
}

/* Initialising trees */

void init_subnets(void)
{
	cp();

	subnet_tree = splay_alloc_tree((splay_compare_t) subnet_compare, (splay_action_t) free_subnet);
}

void exit_subnets(void)
{
	cp();

	splay_delete_tree(subnet_tree);
}

splay_tree_t *new_subnet_tree(void)
{
	cp();

	return splay_alloc_tree((splay_compare_t) subnet_compare, NULL);
}

void free_subnet_tree(splay_tree_t *subnet_tree)
{
	cp();

	splay_delete_tree(subnet_tree);
}

/* Allocating and freeing space for subnets */

subnet_t *new_subnet(void)
{
	cp();

	return xmalloc_and_zero(sizeof(subnet_t));
}

void free_subnet(subnet_t *subnet)
{
	cp();

	free(subnet);
}

/* Adding and removing subnets */

void subnet_add(node_t *n, subnet_t *subnet)
{
	cp();

	subnet->owner = n;

	splay_insert(subnet_tree, subnet);
	splay_insert(n->subnet_tree, subnet);
}

void subnet_del(node_t *n, subnet_t *subnet)
{
	cp();

	splay_delete(n->subnet_tree, subnet);
	splay_delete(subnet_tree, subnet);
}

/* Ascii representation of subnets */

bool str2net(subnet_t *subnet, const char *subnetstr)
{
	int i, l;
	uint16_t x[8];

	cp();

	if(sscanf(subnetstr, "%hu.%hu.%hu.%hu/%d",
			  &x[0], &x[1], &x[2], &x[3], &l) == 5) {
		if(l < 0 || l > 32)
			return false;

		subnet->type = SUBNET_IPV4;
		subnet->net.ipv4.prefixlength = l;

		for(i = 0; i < 4; i++) {
			if(x[i] > 255)
				return false;
			subnet->net.ipv4.address.x[i] = x[i];
		}

		return true;
	}

	if(sscanf(subnetstr, "%hx:%hx:%hx:%hx:%hx:%hx:%hx:%hx/%d",
			  &x[0], &x[1], &x[2], &x[3], &x[4], &x[5], &x[6], &x[7],
			  &l) == 9) {
		if(l < 0 || l > 128)
			return false;

		subnet->type = SUBNET_IPV6;
		subnet->net.ipv6.prefixlength = l;

		for(i = 0; i < 8; i++)
			subnet->net.ipv6.address.x[i] = htons(x[i]);

		return true;
	}

	if(sscanf(subnetstr, "%hu.%hu.%hu.%hu", &x[0], &x[1], &x[2], &x[3]) == 4) {
		subnet->type = SUBNET_IPV4;
		subnet->net.ipv4.prefixlength = 32;

		for(i = 0; i < 4; i++) {
			if(x[i] > 255)
				return false;
			subnet->net.ipv4.address.x[i] = x[i];
		}

		return true;
	}

	if(sscanf(subnetstr, "%hx:%hx:%hx:%hx:%hx:%hx:%hx:%hx",
			  &x[0], &x[1], &x[2], &x[3], &x[4], &x[5], &x[6], &x[7]) == 8) {
		subnet->type = SUBNET_IPV6;
		subnet->net.ipv6.prefixlength = 128;

		for(i = 0; i < 8; i++)
			subnet->net.ipv6.address.x[i] = htons(x[i]);

		return true;
	}

	if(sscanf(subnetstr, "%hx:%hx:%hx:%hx:%hx:%hx",
			  &x[0], &x[1], &x[2], &x[3], &x[4], &x[5]) == 6) {
		subnet->type = SUBNET_MAC;

		for(i = 0; i < 6; i++)
			subnet->net.mac.address.x[i] = x[i];

		return true;
	}

	return false;
}

bool net2str(char *netstr, int len, const subnet_t *subnet)
{
	cp();

	if(!netstr || !subnet) {
		logger(LOG_ERR, _("net2str() was called with netstr=%p, subnet=%p!\n"), netstr, subnet);
		return false;
	}

	switch (subnet->type) {
		case SUBNET_MAC:
			snprintf(netstr, len, "%hx:%hx:%hx:%hx:%hx:%hx",
					 subnet->net.mac.address.x[0],
					 subnet->net.mac.address.x[1],
					 subnet->net.mac.address.x[2],
					 subnet->net.mac.address.x[3],
					 subnet->net.mac.address.x[4], subnet->net.mac.address.x[5]);
			break;

		case SUBNET_IPV4:
			snprintf(netstr, len, "%hu.%hu.%hu.%hu/%d",
					 subnet->net.ipv4.address.x[0],
					 subnet->net.ipv4.address.x[1],
					 subnet->net.ipv4.address.x[2],
					 subnet->net.ipv4.address.x[3], subnet->net.ipv4.prefixlength);
			break;

		case SUBNET_IPV6:
			snprintf(netstr, len, "%hx:%hx:%hx:%hx:%hx:%hx:%hx:%hx/%d",
					 ntohs(subnet->net.ipv6.address.x[0]),
					 ntohs(subnet->net.ipv6.address.x[1]),
					 ntohs(subnet->net.ipv6.address.x[2]),
					 ntohs(subnet->net.ipv6.address.x[3]),
					 ntohs(subnet->net.ipv6.address.x[4]),
					 ntohs(subnet->net.ipv6.address.x[5]),
					 ntohs(subnet->net.ipv6.address.x[6]),
					 ntohs(subnet->net.ipv6.address.x[7]),
					 subnet->net.ipv6.prefixlength);
			break;

		default:
			logger(LOG_ERR,
				   _("net2str() was called with unknown subnet type %d, exiting!"),
				   subnet->type);
			cp_trace();
			exit(0);
	}

	return true;
}

/* Subnet lookup routines */

subnet_t *lookup_subnet(const node_t *owner, const subnet_t *subnet)
{
	cp();

	return splay_search(owner->subnet_tree, subnet);
}

subnet_t *lookup_subnet_mac(const mac_t *address)
{
	subnet_t *p, subnet = {0};

	cp();

	subnet.type = SUBNET_MAC;
	subnet.net.mac.address = *address;
	subnet.owner = NULL;

	p = splay_search(subnet_tree, &subnet);

	return p;
}

subnet_t *lookup_subnet_ipv4(const ipv4_t *address)
{
	subnet_t *p, subnet = {0};

	cp();

	subnet.type = SUBNET_IPV4;
	subnet.net.ipv4.address = *address;
	subnet.net.ipv4.prefixlength = 32;
	subnet.owner = NULL;

	do {
		/* Go find subnet */

		p = splay_search_closest_smaller(subnet_tree, &subnet);

		/* Check if the found subnet REALLY matches */

		if(p) {
			if(p->type != SUBNET_IPV4) {
				p = NULL;
				break;
			}

			if(!maskcmp(address, &p->net.ipv4.address, p->net.ipv4.prefixlength))
				break;
			else {
				/* Otherwise, see if there is a bigger enclosing subnet */

				subnet.net.ipv4.prefixlength = p->net.ipv4.prefixlength - 1;
				if(subnet.net.ipv4.prefixlength < 0 || subnet.net.ipv4.prefixlength > 32)
					return NULL;
				maskcpy(&subnet.net.ipv4.address, &p->net.ipv4.address, subnet.net.ipv4.prefixlength, sizeof subnet.net.ipv4.address);
			}
		}
	} while(p);

	return p;
}

subnet_t *lookup_subnet_ipv6(const ipv6_t *address)
{
	subnet_t *p, subnet = {0};

	cp();

	subnet.type = SUBNET_IPV6;
	subnet.net.ipv6.address = *address;
	subnet.net.ipv6.prefixlength = 128;
	subnet.owner = NULL;

	do {
		/* Go find subnet */

		p = splay_search_closest_smaller(subnet_tree, &subnet);

		/* Check if the found subnet REALLY matches */

		if(p) {
			if(p->type != SUBNET_IPV6)
				return NULL;

			if(!maskcmp(address, &p->net.ipv6.address, p->net.ipv6.prefixlength))
				break;
			else {
				/* Otherwise, see if there is a bigger enclosing subnet */

				subnet.net.ipv6.prefixlength = p->net.ipv6.prefixlength - 1;
				if(subnet.net.ipv6.prefixlength < 0 || subnet.net.ipv6.prefixlength > 128)
					return NULL;
				maskcpy(&subnet.net.ipv6.address, &p->net.ipv6.address, subnet.net.ipv6.prefixlength, sizeof subnet.net.ipv6.address);
			}
		}
	} while(p);

	return p;
}

void subnet_update(node_t *owner, subnet_t *subnet, bool up) {
	splay_node_t *node;
	int i;
	char *envp[8];
	char netstr[MAXNETSTR + 7] = "SUBNET=";
	char *name, *address, *port;

	asprintf(&envp[0], "NETNAME=%s", netname ? : "");
	asprintf(&envp[1], "DEVICE=%s", device ? : "");
	asprintf(&envp[2], "INTERFACE=%s", iface ? : "");
	asprintf(&envp[3], "NODE=%s", owner->name);

	if(owner != myself) {
		sockaddr2str(&owner->address, &address, &port);
		asprintf(&envp[4], "REMOTEADDRESS=%s", address);
		asprintf(&envp[5], "REMOTEPORT=%s", port);
		envp[6] = netstr;
		envp[7] = NULL;
	} else {
		envp[4] = netstr;
		envp[5] = NULL;
	}

	name = up ? "subnet-up" : "subnet-down";

	if(!subnet) {
		for(node = owner->subnet_tree->head; node; node = node->next) {
			subnet = node->data;
			if(!net2str(netstr + 7, sizeof netstr - 7, subnet))
				continue;
			execute_script(name, envp);
		}
	} else {
		if(net2str(netstr + 7, sizeof netstr - 7, subnet))
			execute_script(name, envp);
	}

	for(i = 0; i < (owner != myself ? 6 : 4); i++)
		free(envp[i]);

	if(owner != myself) {
		free(address);
		free(port);
	}
}

int dump_subnets(struct evbuffer *out)
{
	char netstr[MAXNETSTR];
	subnet_t *subnet;
	splay_node_t *node;

	cp();

	for(node = subnet_tree->head; node; node = node->next) {
		subnet = node->data;
		if(!net2str(netstr, sizeof netstr, subnet))
			continue;
		if(evbuffer_add_printf(out, _(" %s owner %s\n"),
							   netstr, subnet->owner->name) == -1)
			return errno;
	}

	return 0;
}
