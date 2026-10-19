/* CRUG channels: chan T, channel, send, recv, close and the select polls.

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

/* A channel type is a pointer to an incomplete record, one record per
   (element type, owns element) pair.  The record is never laid out and is
   not registered in the tag namespace, so a channel is exactly one pointer
   in the ABI (docs/crug/ABI.md) and two element types are different types.
   The runtime copies values through void *: the element size is passed to
   crug_chan_make () and the runtime never learns the element type.

   The compiler declares the runtime entry points itself, as it does for the
   task runtime (crug-tasks.cc), so a Crug program needs no header.  A send
   boxes its value in a compound literal and passes its address; a receive
   returns a pointer to a per-thread buffer which is dereferenced here, so
   the value comes back as an ordinary lvalue of the element type.

   The two _owned entry points carry the same code as their plain twins.  The
   ownership analysis looks up those names to know that a send ends the
   caller's ownership of the value and that a receive hands a value over with
   no known destructor (ADR 0025 D3/D4).  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "target.h"
#include "options.h"
#include "tree.h"
#include "tree-iterator.h"
#include "stringpool.h"
#include "stor-layout.h"
#include "langhooks.h"
#include "c-family/c-common.h"
#include "c-family/crug-common.h"
#include "diagnostic-core.h"
#include "c-tree.h"
#include "crug-chan.h"
#include "crug-send-sync.h"

#include <vector>

bool
crug_channels_enabled (void)
{
  return crug_feature_enabled (CRUG_FEAT_CHANNELS);
}

/* One instantiation of the opaque channel record.  */

struct crug_chan_entry
{
  tree element;
  bool owns;
  tree record;
  tree channel;			/* pointer to RECORD.  */
};

static std::vector<crug_chan_entry> chan_types;

static crug_chan_entry *
chan_find_record (tree record)
{
  for (size_t i = 0; i < chan_types.size (); i++)
    if (chan_types[i].record == record)
      return &chan_types[i];
  return NULL;
}

static crug_chan_entry *
chan_find_channel (tree channel)
{
  if (channel == NULL_TREE || TREE_CODE (channel) != POINTER_TYPE)
    return NULL;
  return chan_find_record (TREE_TYPE (channel));
}

bool
crug_channel_type_p (tree type)
{
  return chan_find_channel (type) != NULL;
}

tree
crug_channel_element (tree type)
{
  crug_chan_entry *e = chan_find_channel (type);
  return e == NULL ? NULL_TREE : e->element;
}

bool
crug_channel_owns_element (tree type)
{
  crug_chan_entry *e = chan_find_channel (type);
  return e == NULL ? false : e->owns;
}

tree
crug_channel_type (tree element, bool owns_element, location_t loc)
{
  if (element == NULL_TREE || element == error_mark_node)
    return error_mark_node;

  for (size_t i = 0; i < chan_types.size (); i++)
    if (chan_types[i].element == element && chan_types[i].owns == owns_element)
      return chan_types[i].channel;

  if (element == void_type_node
      || TREE_CODE (element) == FUNCTION_TYPE
      || !COMPLETE_TYPE_P (element)
      || TREE_CODE (TYPE_SIZE_UNIT (element)) != INTEGER_CST)
    {
      error_at (loc, "a channel element must be a complete type with a size "
		"known at compile time [crug-CONC-019]");
      return error_mark_node;
    }

  /* A channel moves its values between tasks, so its element type must be
     Send (ADR 0026 D4).  An owning channel owns the pointee and the receiver
     gets the pointee, so that is the type to check.  */
  {
    tree probe = (owns_element && TREE_CODE (element) == POINTER_TYPE)
		 ? TREE_TYPE (element) : element;
    tree blocker = NULL_TREE;

    if (!crug_type_send_p (probe, &blocker))
      {
	error_at (loc, "a channel element must be %<Send%>; %qT is not "
		  "[crug-CONC-027]", probe);
	if (blocker != NULL_TREE && blocker != probe)
	  inform (loc, "%qT contains %qT, which is not %<Send%>", probe,
		  blocker);
	return error_mark_node;
      }
  }

  tree record = lang_hooks.types.make_type (RECORD_TYPE);
  tree decl = build_decl (loc, TYPE_DECL, get_identifier ("crug_chan"),
			  record);
  TYPE_NAME (record) = get_identifier ("crug_chan");
  TYPE_STUB_DECL (record) = decl;

  crug_chan_entry e;
  e.element = element;
  e.owns = owns_element;
  e.record = record;
  e.channel = build_pointer_type (record);
  chan_types.push_back (e);
  return e.channel;
}

/* The runtime declarations the compiler emits for itself.  */

static tree chan_make_decl;
static tree chan_send_decl;
static tree chan_send_owned_decl;
static tree chan_recv_ptr_decl;
static tree chan_recv_owned_decl;
static tree chan_close_decl;
static tree chan_recv_ready_decl;
static tree chan_send_ready_decl;
static tree chan_select_turn_decl;
static tree chan_poll_wait_decl;

static tree
chan_runtime_decl (tree *cache, const char *name, tree fntype)
{
  if (*cache == NULL_TREE)
    {
      tree decl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			      get_identifier (name), fntype);
      TREE_PUBLIC (decl) = 1;
      DECL_EXTERNAL (decl) = 1;
      DECL_ARTIFICIAL (decl) = 1;
      TREE_USED (decl) = 1;
      DECL_CONTEXT (decl) = NULL_TREE;
      *cache = decl;
    }
  return *cache;
}

static tree
chan_make_fndecl (void)
{
  tree type = build_function_type_list (ptr_type_node, size_type_node,
					size_type_node, NULL_TREE);
  return chan_runtime_decl (&chan_make_decl, "crug_chan_make", type);
}

static tree
chan_send_fndecl (bool owned)
{
  tree type = build_function_type_list (void_type_node, ptr_type_node,
					ptr_type_node, NULL_TREE);
  if (owned)
    return chan_runtime_decl (&chan_send_owned_decl, "crug_chan_send_owned",
			      type);
  return chan_runtime_decl (&chan_send_decl, "crug_chan_send", type);
}

static tree
chan_recv_fndecl (bool owned)
{
  tree type = build_function_type_list (ptr_type_node, ptr_type_node,
					NULL_TREE);
  if (owned)
    return chan_runtime_decl (&chan_recv_owned_decl, "crug_chan_recv_owned",
			      type);
  return chan_runtime_decl (&chan_recv_ptr_decl, "crug_chan_recv_ptr", type);
}

static tree
chan_close_fndecl (void)
{
  tree type = build_function_type_list (void_type_node, ptr_type_node,
					NULL_TREE);
  return chan_runtime_decl (&chan_close_decl, "crug_chan_close", type);
}

static tree
chan_ready_fndecl (bool receive)
{
  tree type = build_function_type_list (integer_type_node, ptr_type_node,
					NULL_TREE);
  if (receive)
    return chan_runtime_decl (&chan_recv_ready_decl, "crug_chan_recv_ready",
			      type);
  return chan_runtime_decl (&chan_send_ready_decl, "crug_chan_send_ready",
			    type);
}

static tree
chan_select_turn_fndecl (void)
{
  tree type = build_function_type_list (unsigned_type_node, NULL_TREE);
  return chan_runtime_decl (&chan_select_turn_decl, "crug_chan_select_turn",
			    type);
}

static tree
chan_poll_wait_fndecl (void)
{
  tree type = build_function_type_list (void_type_node, NULL_TREE);
  return chan_runtime_decl (&chan_poll_wait_decl, "crug_chan_poll_wait",
			    type);
}

/* Peel the conversions a call argument or a dereference picked up.  */

static tree
chan_peel (tree e)
{
  while (e != NULL_TREE
	 && (TREE_CODE (e) == NOP_EXPR || TREE_CODE (e) == CONVERT_EXPR
	     || TREE_CODE (e) == INDIRECT_REF))
    e = TREE_OPERAND (e, 0);
  return e;
}

static tree
chan_call_fndecl (tree call)
{
  if (call == NULL_TREE || TREE_CODE (call) != CALL_EXPR)
    return NULL_TREE;
  return get_callee_fndecl (call);
}

static bool
chan_call_named (tree call, const char *name)
{
  tree fn = chan_call_fndecl (call);
  if (fn == NULL_TREE || DECL_NAME (fn) == NULL_TREE)
    return false;
  return strcmp (IDENTIFIER_POINTER (DECL_NAME (fn)), name) == 0;
}

tree
crug_channel_make (location_t loc, tree channel_type, tree capacity)
{
  tree element = crug_channel_element (channel_type);
  tree args[2];

  if (element == NULL_TREE)
    return error_mark_node;

  if (capacity == error_mark_node)
    return error_mark_node;

  args[0] = convert (size_type_node, capacity);
  args[1] = TYPE_SIZE_UNIT (element);
  return convert (channel_type,
		  build_call_expr_loc_array (loc, chan_make_fndecl (), 2, args));
}

tree
crug_channel_send (location_t loc, tree channel, tree value)
{
  tree chan_type = TREE_TYPE (channel);
  tree element = crug_channel_element (chan_type);
  bool owned = crug_channel_owns_element (chan_type);
  tree box = NULL_TREE;

  if (element == NULL_TREE || value == error_mark_node)
    return error_mark_node;

  /* A send hands the value to another worker, so the value must be Send and
     must not belong to one thread (ADR 0026 D4/D5).  */
  {
    tree probe = (owned && TREE_CODE (element) == POINTER_TYPE)
		 ? TREE_TYPE (element) : element;
    tree blocker = NULL_TREE;
    const char *affinity = NULL;

    if (crug_expr_thread_affine_p (value, &affinity))
      {
	error_at (loc, "a thread-affine value cannot cross to another task; "
		  "it belongs to the %qs thread [crug-CONC-025]",
		  affinity == NULL ? "unknown" : affinity);
	return error_mark_node;
      }
    if (!crug_type_send_p (probe, &blocker))
      {
	error_at (loc, "a value of type %qT cannot cross to another task; "
		  "the type is not %<Send%> [crug-CONC-024]", probe);
	if (blocker != NULL_TREE && blocker != probe)
	  inform (loc, "%qT contains %qT, which is not %<Send%>", probe,
		  blocker);
	return error_mark_node;
      }
  }

  /* Box the value: the runtime copies it through a pointer, and a compound
     literal is the expression form of "an object of this type, initialized
     here".  The initializer is the value itself, not a CONSTRUCTOR: a
     constructor of scalar type reaches gimplify_init_constructor, whose
     default case is unreachable.  */
  tree elem_value = value;
  if (TREE_TYPE (elem_value) != element)
    elem_value = convert (element, elem_value);
  box = build_compound_literal (loc, element, elem_value, false, 0, NULL);

  tree args[2];
  args[0] = convert (ptr_type_node, channel);
  args[1] = convert (ptr_type_node,
		     build1 (ADDR_EXPR, build_pointer_type (element), box));
  return build_call_expr_loc_array (loc, chan_send_fndecl (owned), 2, args);
}

tree
crug_channel_recv (location_t loc, tree channel)
{
  tree chan_type = TREE_TYPE (channel);
  tree element = crug_channel_element (chan_type);
  bool owned = crug_channel_owns_element (chan_type);

  if (element == NULL_TREE)
    return error_mark_node;

  tree ptr = build_call_expr_loc (loc, chan_recv_fndecl (owned), 1,
				  convert (ptr_type_node, channel));
  ptr = convert (build_pointer_type (element), ptr);
  return build1 (INDIRECT_REF, element, ptr);
}

tree
crug_channel_close (location_t loc, tree channel)
{
  return build_call_expr_loc (loc, chan_close_fndecl (), 1,
			      convert (ptr_type_node, channel));
}

tree
crug_channel_ready (location_t loc, tree channel, bool receive)
{
  return build_call_expr_loc (loc, chan_ready_fndecl (receive), 1,
			      convert (ptr_type_node, channel));
}

tree
crug_channel_select_turn (location_t loc)
{
  return build_call_expr_loc (loc, chan_select_turn_fndecl (), 0);
}

tree
crug_channel_poll_wait (location_t loc)
{
  return build_call_expr_loc (loc, chan_poll_wait_fndecl (), 0);
}

/* The ownership analysis.  A call to crug_chan_send_owned has the element
   expression inside the compound literal of its second argument; the pass
   moves the value out and then replaces it with the plain operand, because a
   compound literal initializer is not walked by the pass (ADR 0025 D3).  */

bool
crug_channel_send_owned_call_p (tree call)
{
  return chan_call_named (call, "crug_chan_send_owned");
}

/* The compound literal that boxes the value of a send call, or NULL_TREE.  */

static tree
chan_send_box (tree call)
{
  tree arg;
  tree box;

  if (call == NULL_TREE || TREE_CODE (call) != CALL_EXPR
      || call_expr_nargs (call) < 2)
    return NULL_TREE;
  arg = chan_peel (CALL_EXPR_ARG (call, 1));
  if (arg == NULL_TREE || TREE_CODE (arg) != ADDR_EXPR)
    return NULL_TREE;
  box = TREE_OPERAND (arg, 0);
  if (box == NULL_TREE || TREE_CODE (box) != COMPOUND_LITERAL_EXPR)
    return NULL_TREE;
  return box;
}

/* The element expression of a send call, or NULL_TREE.  */

tree
crug_channel_send_value (tree call)
{
  tree box = chan_send_box (call);
  tree init;

  if (box == NULL_TREE)
    return NULL_TREE;
  init = DECL_INITIAL (COMPOUND_LITERAL_EXPR_DECL (box));
  if (init != NULL_TREE && TREE_CODE (init) == CONSTRUCTOR
      && CONSTRUCTOR_NELTS (init) == 1)
    return CONSTRUCTOR_ELT (init, 0)->value;
  return init;
}

void
crug_channel_send_set_value (tree call, tree value)
{
  tree box = chan_send_box (call);

  if (box != NULL_TREE)
    DECL_INITIAL (COMPOUND_LITERAL_EXPR_DECL (box)) = value;
}

bool
crug_channel_recv_owned_call_p (tree expr)
{
  tree call = chan_peel (expr);
  return chan_call_named (call, "crug_chan_recv_owned");
}
