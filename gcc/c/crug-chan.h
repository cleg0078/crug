/* CRUG channels: the chan type and the send, recv, close and select
   primitives.  See crug-chan.cc.

   Copyright (C) 2026 CLEG0078

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 3, or (at your option) any later
   version.

   GCC is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING3.  If not see
   <http://www.gnu.org/licenses/>.  */

#ifndef GCC_C_CRUG_CHAN_H
#define GCC_C_CRUG_CHAN_H

/* True when -fcrug-channels is in effect.  */
extern bool crug_channels_enabled (void);

/* The channel type for an element type: one pointer, distinct per element
   type, opaque everywhere else (ADR 0025 D1).  EL is rejected with
   [crug-CONC-019] when it cannot be copied into a channel.  */
extern tree crug_channel_type (tree element, bool owns_element,
			       location_t loc);

extern bool crug_channel_type_p (tree type);
extern tree crug_channel_element (tree type);
extern bool crug_channel_owns_element (tree type);

/* channel (capacity) with its type taken from the declaration or return
   statement being initialized (ADR 0025 D2).  */
extern tree crug_channel_make (location_t loc, tree channel_type,
			       tree capacity);

/* send (ch, value), recv (ch) and close (ch).  A send to a channel whose
   element type is owned boxes the value; the parser has already required
   move () (ADR 0025 D3).  */
extern tree crug_channel_send (location_t loc, tree channel, tree value);
extern tree crug_channel_recv (location_t loc, tree channel);
extern tree crug_channel_close (location_t loc, tree channel);

/* The poll of one select case, the rotation counter that makes each pass
   start with a different case, and the wait between two passes.  */
extern tree crug_channel_ready (location_t loc, tree channel, bool receive);
extern tree crug_channel_select_turn (location_t loc);
extern tree crug_channel_poll_wait (location_t loc);

/* The ownership analysis asks these about an already lowered call: a send
   of an owned element hands the value to the channel, and a receive of one
   hands it back with no known destructor.  */
extern bool crug_channel_send_owned_call_p (tree call);
extern tree crug_channel_send_value (tree call);
extern void crug_channel_send_set_value (tree call, tree value);
extern bool crug_channel_recv_owned_call_p (tree expr);

#endif /* GCC_C_CRUG_CHAN_H */
