/* .crugffi v1: parse, merge, discover and validate C FFI metadata.
   ADR 0007 fixes the grammar and the resolution rules; this file implements
   them for the C front end.  Nothing here changes code generation: the
   metadata is read at compile time only.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "stringpool.h"
#include "c-common.h"
#include "diagnostic-core.h"
#include "crug-common.h"
#include "crug-ffi.h"
#include "cpplib.h"
#include "c-family/c-pragma.h"
#include "line-map.h"
#include "input.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

/* One parsed clause.  ARG is -1 for the return value.  */

struct ffi_op {
  enum ffi_op_kind {
    OP_RETURNS, OP_CONSUMES, OP_BORROWS, OP_MUT_BORROWS, OP_OUT_OWNED,
    OP_CALLBACK, OP_RETAINED, OP_BLOCKING, OP_NULLABLE
  };
  enum ffi_op_kind k;
  int arg;
  bool owned;
  bool nullable;
  std::string type_name;
  std::string tag_kind;
  std::string drop;
  std::string until;
  int line;
};

struct ffi_type_decl {
  std::string name;
  std::string tag_kind;
  std::string drop;
  bool opaque;
  bool send;
  bool sync;
  bool not_send;
  bool not_sync;
  std::string file;
  int line;
  bool explicit_p;
};

struct ffi_fn_decl {
  std::string name;
  std::vector<ffi_op> ops;
  /* 'thread_affinity NAME' names the thread the returned value belongs to
     (ADR 0007, ADR 0026 D7).  */
  std::string affinity;
  std::string file;
  int line;
  bool explicit_p;
  bool resolved;
  bool ignored;
};

static std::vector<std::string> ffi_explicit_paths;
static std::set<std::string> ffi_loaded_files;
static std::map<std::string, ffi_fn_decl> ffi_functions;
static std::map<std::string, ffi_type_decl> ffi_types;
static std::map<std::string, crug_ffi_info> ffi_infos;
static bool ffi_option_seen;

/* Diagnostics.  A .crugffi file is not part of the line map, so the message
   carries its own file:line prefix; the stable code is what the gates and
   the documentation refer to.  */

static void
ffi_file_error (const char *path, int code, const char *fmt, ...)
{
  char msg[512];
  va_list ap;

  char suffix[32];

  va_start (ap, fmt);
  vsnprintf (msg, sizeof msg, fmt, ap);
  va_end (ap);
  /* pretty_printer understands %s and %d but not field widths, so the
     three-digit code is formatted here (DIAGNOSTICS.md section 1).  */
  snprintf (suffix, sizeof suffix, "crug-FFI-%03d", code);
  error ("%s: %s [%s]", path, msg, suffix);
}

static void
ffi_error (const char *path, int line, int code, const char *fmt, ...)
{
  char msg[512];
  va_list ap;

  char suffix[32];

  va_start (ap, fmt);
  vsnprintf (msg, sizeof msg, fmt, ap);
  va_end (ap);
  snprintf (suffix, sizeof suffix, "crug-FFI-%03d", code);
  error ("%s:%d: %s [%s]", path, line, msg, suffix);
}

/* Tokenizer and parser.  */

struct ffi_token {
  std::string text;
  int line;
};

struct ffi_parser {
  const char *path;
  bool explicit_p;
  std::vector<ffi_token> toks;
  size_t pos;
  bool failed;
};

static bool
at_end (const ffi_parser &p)
{
  return p.pos >= p.toks.size ();
}

static bool
at_semi (const ffi_parser &p)
{
  return !at_end (p) && p.toks[p.pos].text == ";";
}

static const char *
peek (const ffi_parser &p)
{
  return at_end (p) ? "" : p.toks[p.pos].text.c_str ();
}

static std::string
take (ffi_parser &p)
{
  if (at_end (p))
    {
      p.failed = true;
      return std::string ();
    }
  return p.toks[p.pos++].text;
}

static bool
accept (ffi_parser &p, const char *word)
{
  if (!at_end (p) && p.toks[p.pos].text == word)
    {
      p.pos++;
      return true;
    }
  return false;
}

static bool
is_word_char (int c)
{
  return ISALNUM (c) || c == '_' || c == '.' || c == '!';
}

/* Read one sidecar and parse its declarations.  Returns false when the file
   was not loaded; the diagnostic has been issued then.  */

static bool parse_ffi_file (const char *path, bool explicit_p);

static std::string
ops_signature (const std::vector<ffi_op> &ops)
{
  std::string s;
  char buf[128];

  for (size_t i = 0; i < ops.size (); i++)
    {
      const ffi_op &o = ops[i];
      snprintf (buf, sizeof buf, "%d/%d/%d/%d/", (int) o.k, o.arg,
		(int) o.owned, (int) o.nullable);
      s += buf;
      s += o.type_name;
      s += "|";
      s += o.tag_kind;
      s += "|";
      s += o.drop;
      s += "|";
      s += o.until;
      s += ";";
    }
  return s;
}

static void
merge_fn (ffi_fn_decl fn)
{
  std::map<std::string, ffi_fn_decl>::iterator it = ffi_functions.find (fn.name);

  if (it == ffi_functions.end ())
    {
      ffi_functions[fn.name] = fn;
      return;
    }
  if (ops_signature (it->second.ops) == ops_signature (fn.ops)
      && it->second.affinity == fn.affinity)
    {
      /* D4: identical declarations merge.  An explicit occurrence wins the
	 origin, so the undeclared-function rule still applies.  */
      if (fn.explicit_p && !it->second.explicit_p)
	it->second.explicit_p = true;
      return;
    }
  ffi_error (fn.file.c_str (), fn.line, 4,
	     "conflicting declarations of '%s': %s:%d differs",
	     fn.name.c_str (), it->second.file.c_str (), it->second.line);
}

static void
merge_type (ffi_type_decl t)
{
  std::map<std::string, ffi_type_decl>::iterator it = ffi_types.find (t.name);

  if (it == ffi_types.end ())
    {
      ffi_types[t.name] = t;
      return;
    }
  const ffi_type_decl &o = it->second;
  if (o.tag_kind == t.tag_kind && o.drop == t.drop && o.opaque == t.opaque
      && o.send == t.send && o.sync == t.sync && o.not_send == t.not_send
      && o.not_sync == t.not_sync)
    return;
  ffi_error (t.file.c_str (), t.line, 4,
	     "conflicting declarations of type '%s': %s:%d differs",
	     t.name.c_str (), o.file.c_str (), o.line);
}

static bool
check_repeated_op (ffi_parser &p, const ffi_fn_decl &fn, const ffi_op &op)
{
  for (size_t i = 0; i < fn.ops.size (); i++)
    if (fn.ops[i].k == op.k && fn.ops[i].arg == op.arg)
      {
	ffi_error (p.path, op.line, 3,
		   "clause repeats an earlier clause for the same operand");
	p.failed = true;
	return false;
      }
  return true;
}

static void
parse_ctype (ffi_parser &p, ffi_op &op)
{
  std::string w = take (p);

  if (w == "void")
    op.type_name = "void";
  else if (w == "struct" || w == "union" || w == "enum")
    {
      op.tag_kind = w;
      op.type_name = take (p);
    }
  else
    op.type_name = w;
}

static void
parse_op_modifiers (ffi_parser &p, ffi_op &op)
{
  for (;;)
    {
      const char *w = peek (p);
      if (strcmp (w, "nullable") == 0)
	{
	  p.pos++;
	  op.nullable = true;
	}
      else if (strcmp (w, "drop") == 0)
	{
	  p.pos++;
	  op.drop = take (p);
	}
      else if (strcmp (w, "until") == 0)
	{
	  p.pos++;
	  op.until = take (p);
	}
      else
	break;
    }
}

/* Parse an argument reference: 'ret', 'arg0'... or 'arg 0'...  The index
   is decimal digits only: no sign, no empty index and no trailing character
   ('arg1x' used to become argument 1 through atoi), and it must fit in an
   int (P3-006).  The range against the declared function stays in
   resolve_fn.  */

static bool
parse_argref (ffi_parser &p, int *arg)
{
  const int line = p.toks[p.pos].line;
  std::string w = take (p);
  const char *digits = NULL;

  if (w == "ret")
    {
      *arg = -1;
      return true;
    }
  if (w == "arg")
    {
      std::string n = take (p);
      digits = n.c_str ();
    }
  else if (w.compare (0, 3, "arg") == 0 && w.size () > 3)
    digits = w.c_str () + 3;

  if (digits != NULL)
    {
      if (*digits == '\0')
	{
	  ffi_error (p.path, line, 3,
		     "'%s' needs an argument index: write 'argN'", w.c_str ());
	  p.failed = true;
	  return false;
	}
      long v = 0;
      for (const char *d = digits; *d != '\0'; d++)
	{
	  if (*d < '0' || *d > '9' || v > (INT_MAX - (*d - '0')) / 10)
	    {
	      ffi_error (p.path, line, 3,
			 "'%s' is not an argument index: write 'argN' with "
			 "decimal digits", w.c_str ());
	      p.failed = true;
	      return false;
	    }
	  v = v * 10 + (*d - '0');
	}
      *arg = (int) v;
      return true;
    }

  ffi_error (p.path, line, 3, "expected 'argN' or 'ret', found '%s'",
	     w.c_str ());
  p.failed = true;
  return false;
}

static bool
parse_fn_ops (ffi_parser &p, ffi_fn_decl &fn)
{
  while (!at_semi (p) && !at_end (p))
    {
      ffi_op op;
      int line = p.toks[p.pos].line;
      std::string w = take (p);

      op.arg = -1;
      op.owned = false;
      op.nullable = false;
      op.line = line;
      if (w == "returns")
	{
	  op.k = ffi_op::OP_RETURNS;
	  if (accept (p, "owned"))
	    op.owned = true;
	  parse_ctype (p, op);
	}
      else if (w == "consumes" || w == "borrows" || w == "mut_borrows"
	       || w == "out_owned" || w == "callback" || w == "retained"
	       || w == "blocking")
	{
	  if (w == "consumes")
	    op.k = ffi_op::OP_CONSUMES;
	  else if (w == "borrows")
	    op.k = ffi_op::OP_BORROWS;
	  else if (w == "mut_borrows")
	    op.k = ffi_op::OP_MUT_BORROWS;
	  else if (w == "out_owned")
	    op.k = ffi_op::OP_OUT_OWNED;
	  else if (w == "callback")
	    op.k = ffi_op::OP_CALLBACK;
	  else if (w == "retained")
	    op.k = ffi_op::OP_RETAINED;
	  else
	    op.k = ffi_op::OP_BLOCKING;
	  if (!parse_argref (p, &op.arg))
	    return false;
	}
      else if (w == "thread_affinity")
	{
	  std::string aff = take (p);
	  if (aff.empty ())
	    {
	      ffi_error (p.path, line, 3,
			 "'thread_affinity' needs a thread name");
	      p.failed = true;
	      return false;
	    }
	  if (!fn.affinity.empty ())
	    {
	      ffi_error (p.path, line, 3,
			 "'thread_affinity' is stated twice");
	      p.failed = true;
	      return false;
	    }
	  fn.affinity = aff;
	  continue;
	}
      else if (w == "nullable")
	{
	  op.k = ffi_op::OP_NULLABLE;
	  if (!parse_argref (p, &op.arg))
	    return false;
	}
      else
	{
	  ffi_error (p.path, line, 3, "unknown clause '%s'", w.c_str ());
	  p.failed = true;
	  return false;
	}
      parse_op_modifiers (p, op);
      if (!check_repeated_op (p, fn, op))
	return false;
      fn.ops.push_back (op);
    }
  if (p.failed)
    return false;
  if (!at_semi (p))
    {
      ffi_error (p.path, p.toks.back ().line, 1,
		 "expected ';' after the declaration");
      p.failed = true;
      return false;
    }
  return true;
}

static void
parse_fn (ffi_parser &p, bool explicit_p)
{
  ffi_fn_decl fn;

  fn.line = at_end (p) ? 0 : p.toks[p.pos].line;
  fn.file = p.path;
  fn.explicit_p = explicit_p;
  fn.resolved = false;
  fn.ignored = false;
  fn.name = take (p);
  if (fn.name.empty ())
    {
      ffi_error (p.path, fn.line, 1, "expected a function name after 'fn'");
      p.failed = true;
      return;
    }
  if (!parse_fn_ops (p, fn))
    return;
  merge_fn (fn);
}

static void
parse_type (ffi_parser &p, bool explicit_p)
{
  ffi_type_decl t;
  int line = at_end (p) ? 0 : p.toks[p.pos].line;
  std::string w = take (p);

  t.drop = "";
  t.opaque = false;
  t.send = false;
  t.sync = false;
  t.not_send = false;
  t.not_sync = false;
  t.file = p.path;
  t.line = line;
  t.explicit_p = explicit_p;
  if (w == "struct" || w == "union" || w == "enum" || w == "opaque")
    {
      t.tag_kind = w;
      t.name = take (p);
      if (w == "opaque")
	t.opaque = true;
    }
  else
    t.name = w;

  if (t.name == "void")
    {
      ffi_error (p.path, line, 2, "'type void' is not a valid declaration");
      p.failed = true;
      return;
    }
  while (!at_semi (p) && !at_end (p))
    {
      std::string c = take (p);
      if (c == "drop")
	t.drop = take (p);
      else if (c == "send")
	t.send = true;
      else if (c == "sync")
	t.sync = true;
      else if (c == "!send")
	t.not_send = true;
      else if (c == "!sync")
	t.not_sync = true;
      else
	{
	  ffi_error (p.path, line, 3, "unknown type clause '%s'", c.c_str ());
	  p.failed = true;
	  return;
	}
    }
  if (p.failed)
    return;
  if (!at_semi (p))
    {
      ffi_error (p.path, p.toks.back ().line, 1,
		 "expected ';' after the declaration");
      p.failed = true;
      return;
    }
  if (!t.tag_kind.empty () && t.tag_kind != "opaque" && t.name.empty ())
    {
      ffi_error (p.path, line, 1, "expected a type name");
      p.failed = true;
      return;
    }
  merge_type (t);
}

static bool
parse_ffi_file (const char *path, bool explicit_p)
{
  if (ffi_loaded_files.find (std::string (path)) != ffi_loaded_files.end ())
    return true;

  FILE *f = fopen (path, "r");
  if (f == NULL)
    {
      if (explicit_p)
	ffi_file_error (path, 1, "cannot read metadata file: %m");
      return false;
    }

  std::vector<ffi_token> toks;
  char *line = NULL;
  size_t cap = 0;
  ssize_t len;
  int lineno = 0;
  long bytes = 0;
  bool too_big = false;

  while ((len = getline (&line, &cap, f)) >= 0)
    {
      lineno++;
      bytes += (long) len;
      if (bytes > 1024 * 1024)
	{
	  ffi_file_error (path, 10, "metadata file exceeds 1 MiB");
	  too_big = true;
	  break;
	}
      for (ssize_t i = 0; i < len; i++)
	if (line[i] == '#')
	  {
	    line[i] = '\0';
	    break;
	  }
      for (ssize_t i = 0; i < len && line[i] != '\0'; )
	{
	  if (ISSPACE ((unsigned char) line[i]))
	    {
	      i++;
	      continue;
	    }
	  ssize_t j = i;
	  while (j < len && line[j] != '\0'
		 && is_word_char ((unsigned char) line[j]))
	    j++;
	  if (j == i)
	    {
	      if (line[i] == ';')
		{
		  ffi_token t;
		  t.text = ";";
		  t.line = lineno;
		  toks.push_back (t);
		  i++;
		  continue;
		}
	      ffi_error (path, lineno, 1, "unexpected character '%c'",
			 line[i]);
	      free (line);
	      fclose (f);
	      return false;
	    }
	  ffi_token t;
	  t.text = std::string (line + i, (size_t) (j - i));
	  t.line = lineno;
	  toks.push_back (t);
	  i = j;
	}
    }
  free (line);
  fclose (f);
  if (too_big)
    return false;

  ffi_parser p;
  p.path = path;
  p.explicit_p = explicit_p;
  p.toks = toks;
  p.pos = 0;
  p.failed = false;

  if (p.toks.empty ())
    {
      ffi_error (path, 1, 1, "missing 'crugffi <version>;' header");
      return false;
    }
  if (p.toks[0].text != "crugffi" || p.toks.size () < 3
      || p.toks[2].text != ";")
    {
      ffi_error (path, p.toks[0].line, 1,
		 "missing 'crugffi <version>;' header");
      return false;
    }

  const char *ver = p.toks[1].text.c_str ();
  char *end = NULL;
  long major = strtol (ver, &end, 10);
  long minor = 0;
  if (end == ver || *end != '.')
    {
      ffi_error (path, p.toks[1].line, 1, "malformed version '%s'", ver);
      return false;
    }
  minor = strtol (end + 1, &end, 10);
  if (*end != '\0')
    {
      ffi_error (path, p.toks[1].line, 1, "malformed version '%s'", ver);
      return false;
    }
  if (major != 1)
    {
      ffi_error (path, p.toks[1].line, 1,
		 "major version %ld is newer than this compiler supports (1)",
		 major);
      return false;
    }

  p.pos = 3;
  while (!at_end (p) && !p.failed)
    {
      if (at_semi (p))
	{
	  p.pos++;
	  continue;
	}
      int line_no = p.toks[p.pos].line;
      std::string kw = take (p);
      if (kw == "fn")
	{
	  int before = (int) p.pos;
	  parse_fn (p, explicit_p);
	  if (p.failed)
	    break;
	  if (!at_semi (p) || (int) p.pos == before)
	    {
	      ffi_error (path, line_no, 1,
			 "expected ';' after the function declaration");
	      p.failed = true;
	      break;
	    }
	  p.pos++;
	}
      else if (kw == "type")
	{
	  parse_type (p, explicit_p);
	  if (p.failed)
	    break;
	  if (!at_semi (p))
	    {
	      ffi_error (path, line_no, 1,
			 "expected ';' after the type declaration");
	      p.failed = true;
	      break;
	    }
	  p.pos++;
	}
      else
	{
	  ffi_error (path, line_no, 1, "unknown declaration '%s'",
		     kw.c_str ());
	  p.failed = true;
	  break;
	}
    }
  if (p.failed)
    return false;

  if (ffi_functions.size () + ffi_types.size () > 10000)
    {
      ffi_file_error (path, 10, "metadata declares more than 10000 entries");
      return false;
    }

  ffi_loaded_files.insert (std::string (path));
  return true;
}

/* Resolution against the translation unit.  */

static tree
lookup_typedef_type (const char *name)
{
  tree d = lookup_name (get_identifier (name));

  if (d != NULL_TREE && TREE_CODE (d) == TYPE_DECL)
    return TREE_TYPE (d);
  return NULL_TREE;
}

static bool
ffi_type_matches (tree type, const std::string &name, const std::string &tag_kind)
{
  if (name == "void")
    return VOID_TYPE_P (type);

  tree main = TYPE_MAIN_VARIANT (type);

  if (!tag_kind.empty () && tag_kind != "opaque")
    {
      if (tag_kind == "struct" && TREE_CODE (main) != RECORD_TYPE)
	return false;
      if (tag_kind == "union" && TREE_CODE (main) != UNION_TYPE)
	return false;
      if (tag_kind == "enum" && TREE_CODE (main) != ENUMERAL_TYPE)
	return false;
    }

  tree td = lookup_typedef_type (name.c_str ());
  if (td != NULL_TREE && TYPE_MAIN_VARIANT (td) == main)
    return true;

  if (!tag_kind.empty ())
    {
      /* A tagged type carries its name either as a TYPE_DECL (the C++ and
	 the usual C case) or, in this front end, directly as an identifier
	 node; accept both.  */
      tree tn = TYPE_NAME (main);
      if (tn != NULL_TREE)
	{
	  const char *nm = NULL;
	  if (TREE_CODE (tn) == TYPE_DECL)
	    nm = IDENTIFIER_POINTER (DECL_NAME (tn));
	  else if (TREE_CODE (tn) == IDENTIFIER_NODE)
	    nm = IDENTIFIER_POINTER (tn);
	  if (nm != NULL && strcmp (nm, name.c_str ()) == 0)
	    return true;
	}
    }
  return false;
}

static bool
pointer_to_matches (tree ptr, tree pointee)
{
  if (ptr == NULL_TREE || TREE_CODE (ptr) != POINTER_TYPE)
    return false;
  tree t = TREE_TYPE (ptr);
  if (VOID_TYPE_P (t) || VOID_TYPE_P (pointee))
    return true;
  return TYPE_MAIN_VARIANT (t) == TYPE_MAIN_VARIANT (pointee);
}

/* A destructor is called with exactly one pointer, so its declaration has
   to say so.  The argument list is checked, not the return type: C explicitly
   allows a value-returning function to be called as a statement, and standard
   destructors such as fclose return int.  An old-style declaration accepts
   any argument list and a variadic one takes more, so both are rejected
   (P3-005).  */

static tree
resolve_dtor (const std::string &name, tree pointee, const std::string &file,
	      int line, const std::string &what)
{
  if (name.empty ())
    return NULL_TREE;

  tree fn = lookup_name (get_identifier (name.c_str ()));
  if (fn == NULL_TREE || TREE_CODE (fn) != FUNCTION_DECL || !TREE_PUBLIC (fn))
    {
      ffi_error (file.c_str (), line, 11,
		 "destructor '%s' for %s is not an external function",
		 name.c_str (), what.c_str ());
      return NULL_TREE;
    }

  tree fntype = TREE_TYPE (fn);
  if (!prototype_p (fntype))
    {
      ffi_error (file.c_str (), line, 11,
		 "destructor '%s' for %s must be declared with a prototype "
		 "taking one pointer argument",
		 name.c_str (), what.c_str ());
      return NULL_TREE;
    }
  if (stdarg_p (fntype))
    {
      ffi_error (file.c_str (), line, 11,
		 "destructor '%s' for %s must not be variadic",
		 name.c_str (), what.c_str ());
      return NULL_TREE;
    }

  tree args = TYPE_ARG_TYPES (fntype);
  if (args == NULL_TREE || args == void_list_node
      || TREE_CODE (args) == VOID_TYPE)
    {
      ffi_error (file.c_str (), line, 11,
		 "destructor '%s' for %s must take one pointer argument",
		 name.c_str (), what.c_str ());
      return NULL_TREE;
    }
  if (TREE_CHAIN (args) != void_list_node)
    {
      ffi_error (file.c_str (), line, 11,
		 "destructor '%s' for %s must take exactly one argument",
		 name.c_str (), what.c_str ());
      return NULL_TREE;
    }
  if (!pointer_to_matches (TREE_VALUE (args), pointee))
    {
      ffi_error (file.c_str (), line, 11,
		 "destructor '%s' for %s does not take a pointer to it",
		 name.c_str (), what.c_str ());
      return NULL_TREE;
    }
  return fn;
}

static tree
find_type_dtor (tree pointee)
{
  std::map<std::string, ffi_type_decl>::iterator it;
  for (it = ffi_types.begin (); it != ffi_types.end (); ++it)
    if (ffi_type_matches (pointee, it->second.name, it->second.tag_kind))
      return resolve_dtor (it->second.drop, pointee, it->second.file,
			   it->second.line, it->first);
  return NULL_TREE;
}

static const crug_ffi_info *
resolve_fn (ffi_fn_decl &decl)
{
  std::map<std::string, crug_ffi_info>::iterator info_it;

  if (decl.resolved)
    {
      if (decl.ignored)
	return NULL;
      info_it = ffi_infos.find (decl.name);
      return info_it == ffi_infos.end () ? NULL : &info_it->second;
    }
  decl.resolved = true;

  tree fn = lookup_name (get_identifier (decl.name.c_str ()));
  if (fn == NULL_TREE)
    {
      if (decl.explicit_p)
	ffi_error (decl.file.c_str (), decl.line, 6,
		   "metadata declares '%s', which the translation unit does "
		   "not declare", decl.name.c_str ());
      decl.ignored = true;
      return NULL;
    }
  if (TREE_CODE (fn) != FUNCTION_DECL || !TREE_PUBLIC (fn))
    {
      if (decl.explicit_p)
	ffi_error (decl.file.c_str (), decl.line, 6,
		   "'%s' is not an external function; its metadata is not used",
		   decl.name.c_str ());
      decl.ignored = true;
      return NULL;
    }

  tree fn_type = TREE_TYPE (fn);
  tree args = TYPE_ARG_TYPES (fn_type);
  if (args == NULL_TREE)
    {
      ffi_error (decl.file.c_str (), decl.line, 6,
		 "'%s' has no prototype; metadata needs named parameters",
		 decl.name.c_str ());
      decl.ignored = true;
      return NULL;
    }
  unsigned int nargs = 0;
  bool variadic = stdarg_p (fn_type);

  for (tree a = args; a != NULL_TREE && TREE_CODE (a) != VOID_TYPE;
       a = TREE_CHAIN (a))
    nargs++;

  crug_ffi_info info;
  info.fndecl = fn;
  info.returns_owned = false;
  info.returns_nullable = false;
  info.owned_type = NULL_TREE;
  info.dtor = NULL_TREE;
  info.consumes.assign (nargs, false);
  info.borrows.assign (nargs, false);
  info.mut_borrows.assign (nargs, false);
  info.out_owned.assign (nargs, false);
  info.callback.assign (nargs, false);
  info.nullable.assign (nargs, false);
  info.out_dtor.assign (nargs, NULL_TREE);
  info.thread_affine = !decl.affinity.empty ();
  info.affinity = decl.affinity;

  /* First pass: shape checks that do not need the return type.  */
  for (size_t i = 0; i < decl.ops.size (); i++)
    {
      const ffi_op &op = decl.ops[i];
      if (op.k == ffi_op::OP_RETURNS || op.arg < 0)
	continue;
      if ((unsigned int) op.arg >= nargs)
	{
	  ffi_error (decl.file.c_str (), op.line, 5,
		     "argument %d is out of range for '%s' (%u parameters%s)",
		     op.arg, decl.name.c_str (), nargs,
		     variadic ? ", variadic" : "");
	  continue;
	}
      tree pt = TREE_VALUE (tree_last (args));
      /* Walk to the op.arg-th parameter.  */
      tree a = args;
      for (int k = 0; k < op.arg; k++)
	a = TREE_CHAIN (a);
      pt = TREE_VALUE (a);
      bool needs_pointer = op.k == ffi_op::OP_CONSUMES
			   || op.k == ffi_op::OP_BORROWS
			   || op.k == ffi_op::OP_MUT_BORROWS
			   || op.k == ffi_op::OP_OUT_OWNED
			   || op.k == ffi_op::OP_CALLBACK
			   || op.nullable;
      if (needs_pointer && TREE_CODE (pt) != POINTER_TYPE)
	{
	  ffi_error (decl.file.c_str (), op.line, 6,
		     "argument %d of '%s' is not a pointer, so it cannot "
		     "carry an ownership clause", op.arg,
		     decl.name.c_str ());
	  continue;
	}
      if (op.k == ffi_op::OP_OUT_OWNED
	  && TREE_CODE (TREE_TYPE (pt)) != POINTER_TYPE)
	{
	  ffi_error (decl.file.c_str (), op.line, 6,
		     "'out_owned' needs a pointer-to-pointer argument; "
		     "argument %d of '%s' is not one", op.arg,
		     decl.name.c_str ());
	  continue;
	}
      if (op.k == ffi_op::OP_CALLBACK
	  && TREE_CODE (TREE_TYPE (pt)) != FUNCTION_TYPE)
	{
	  ffi_error (decl.file.c_str (), op.line, 6,
		     "'callback' needs a function pointer; argument %d of "
		     "'%s' is not one", op.arg, decl.name.c_str ());
	  continue;
	}
      switch (op.k)
	{
	case ffi_op::OP_CONSUMES:
	  info.consumes[op.arg] = true;
	  break;
	case ffi_op::OP_BORROWS:
	  info.borrows[op.arg] = true;
	  break;
	case ffi_op::OP_MUT_BORROWS:
	  info.mut_borrows[op.arg] = true;
	  break;
	case ffi_op::OP_OUT_OWNED:
	  info.out_owned[op.arg] = true;
	  if (!op.drop.empty ())
	    info.out_dtor[op.arg]
	      = resolve_dtor (op.drop, TREE_TYPE (TREE_TYPE (pt)),
			      decl.file, op.line, decl.name);
	  break;
	case ffi_op::OP_CALLBACK:
	  info.callback[op.arg] = true;
	  break;
	default:
	  break;
	}
      if (op.nullable)
	info.nullable[op.arg] = true;
    }

  /* Conflicting clauses on one operand (D4).  */
  for (unsigned int i = 0; i < nargs; i++)
    {
      if (info.borrows[i] && info.mut_borrows[i])
	ffi_error (decl.file.c_str (), decl.line, 4,
		   "'%s': argument %u is both 'borrows' and 'mut_borrows'",
		   decl.name.c_str (), i);
      if ((info.borrows[i] || info.mut_borrows[i])
	  && (info.consumes[i] || info.out_owned[i]))
	ffi_error (decl.file.c_str (), decl.line, 4,
		   "'%s': argument %u is borrowed and also transferred",
		   decl.name.c_str (), i);
      if (info.callback[i] && info.out_owned[i])
	ffi_error (decl.file.c_str (), decl.line, 4,
		   "'%s': argument %u is both a callback and 'out_owned'",
		   decl.name.c_str (), i);
    }

  /* The return value.  */
  for (size_t i = 0; i < decl.ops.size (); i++)
    {
      const ffi_op &op = decl.ops[i];
      if (op.k != ffi_op::OP_RETURNS)
	continue;
      if (!op.owned)
	{
	  info.returns_nullable = op.nullable;
	  continue;
	}
      /* FN is the FUNCTION_DECL; its type is the FUNCTION_TYPE whose own
	 type is the return type.  */
      tree ret = TREE_TYPE (TREE_TYPE (fn));
      if (TREE_CODE (ret) != POINTER_TYPE)
	{
	  ffi_error (decl.file.c_str (), op.line, 6,
		     "'returns owned' needs a pointer return type; '%s' "
		     "returns by value", decl.name.c_str ());
	  continue;
	}
      tree pointee = TREE_TYPE (ret);
      if (!ffi_type_matches (pointee, op.type_name, op.tag_kind))
	{
	  ffi_error (decl.file.c_str (), op.line, 6,
		     "'%s' does not return '%s'", decl.name.c_str (),
		     op.type_name.c_str ());
	  continue;
	}
      info.returns_owned = true;
      info.returns_nullable = op.nullable;
      info.owned_type = pointee;
      if (!op.drop.empty ())
	info.dtor = resolve_dtor (op.drop, pointee, decl.file, op.line,
				  decl.name);
      else
	info.dtor = find_type_dtor (pointee);
      if (info.dtor == NULL_TREE)
	ffi_error (decl.file.c_str (), op.line, 11,
		   "no destructor is determinable for the owned return of "
		   "'%s'; name one with 'drop'", decl.name.c_str ());
    }

  /* Two declarations of one function with different destructors cannot
     both be honoured; the merge already rejected differing clause sets,
     so this only catches 'drop' disagreement through type declarations.  */
  ffi_infos[decl.name] = info;
  return &ffi_infos[decl.name];
}

const crug_ffi_info *
crug_ffi_lookup (tree fndecl)
{
  if (fndecl == NULL_TREE || TREE_CODE (fndecl) != FUNCTION_DECL
      || DECL_NAME (fndecl) == NULL_TREE || ffi_functions.empty ())
    return NULL;
  const char *name = IDENTIFIER_POINTER (DECL_NAME (fndecl));
  std::map<std::string, ffi_fn_decl>::iterator it = ffi_functions.find (name);
  if (it == ffi_functions.end ())
    return NULL;
  return resolve_fn (it->second);
}

bool
crug_ffi_returns_owned (tree fndecl, tree *dtor)
{
  const crug_ffi_info *info = crug_ffi_lookup (fndecl);

  if (info == NULL || !info->returns_owned)
    return false;
  if (dtor != NULL)
    *dtor = info->dtor;
  return true;
}

bool
crug_ffi_arg_consumes (tree fndecl, unsigned int idx)
{
  const crug_ffi_info *info = crug_ffi_lookup (fndecl);

  return info != NULL && idx < info->consumes.size () && info->consumes[idx];
}

bool
crug_ffi_arg_out_owned (tree fndecl, unsigned int idx, tree *dtor)
{
  const crug_ffi_info *info = crug_ffi_lookup (fndecl);

  if (info == NULL || idx >= info->out_owned.size () || !info->out_owned[idx])
    return false;
  if (dtor != NULL)
    *dtor = info->out_dtor[idx];
  return true;
}

bool
crug_ffi_arg_callback (tree fndecl, unsigned int idx)
{
  const crug_ffi_info *info = crug_ffi_lookup (fndecl);

  return info != NULL && idx < info->callback.size () && info->callback[idx];
}

bool
crug_ffi_arg_borrow (tree fndecl, unsigned int idx, bool *mut_p)
{
  const crug_ffi_info *info = crug_ffi_lookup (fndecl);

  if (info == NULL || idx >= info->borrows.size ())
    return false;
  if (info->mut_borrows[idx])
    {
      if (mut_p != NULL)
	*mut_p = true;
      return true;
    }
  if (info->borrows[idx])
    {
      if (mut_p != NULL)
	*mut_p = false;
      return true;
    }
  return false;
}

bool
crug_ffi_arg_nullable (tree fndecl, unsigned int idx)
{
  const crug_ffi_info *info = crug_ffi_lookup (fndecl);

  return info != NULL && idx < info->nullable.size () && info->nullable[idx];
}

bool
crug_ffi_thread_affine (tree fndecl, const char **name)
{
  const crug_ffi_info *info = crug_ffi_lookup (fndecl);

  if (info == NULL || !info->thread_affine)
    return false;
  if (name != NULL)
    *name = info->affinity.c_str ();
  return true;
}

/* The explicit send/sync annotations of TYPE.  A type declaration names a
   type either through its tag or through a typedef; ffi_type_matches
   already knows both, so this is a scan over the few annotated types.  */

bool
crug_ffi_type_flags (tree type, bool *send, bool *sync,
		     bool *not_send, bool *not_sync)
{
  bool found = false;

  if (send != NULL)
    *send = false;
  if (sync != NULL)
    *sync = false;
  if (not_send != NULL)
    *not_send = false;
  if (not_sync != NULL)
    *not_sync = false;
  if (type == NULL_TREE || type == error_mark_node || ffi_types.empty ())
    return false;

  for (std::map<std::string, ffi_type_decl>::const_iterator it
	 = ffi_types.begin (); it != ffi_types.end (); ++it)
    {
      const ffi_type_decl &t = it->second;

      if (!t.send && !t.sync && !t.not_send && !t.not_sync)
	continue;
      if (!ffi_type_matches (type, t.name, t.tag_kind))
	continue;
      found = true;
      if (t.send && send != NULL)
	*send = true;
      if (t.sync && sync != NULL)
	*sync = true;
      if (t.not_send && not_send != NULL)
	*not_send = true;
      if (t.not_sync && not_sync != NULL)
	*not_sync = true;
    }
  return found;
}

/* Options and discovery.  */

bool
crug_ffi_enabled_p (void)
{
  return ffi_option_seen;
}

void
crug_ffi_handle_option (const char *path)
{
  ffi_option_seen = true;
  if (path == NULL || path[0] == '\0')
    return;
  ffi_explicit_paths.push_back (path);
  parse_ffi_file (path, true);
}

void
crug_ffi_enable_discovery (void)
{
  ffi_option_seen = true;
}

/* The include callback's 'dir' argument is the directive name ("include"),
   not a directory: libcpp passes pfile->directive->name.  The directory of the
   file that contains the #include line comes from the linemap instead, and
   that is where an adjacent sidecar is looked for (ADR 0007 D5, discovery
   step 2, amendment A-1).  */

static void
discover_for_include (const char *includer, const char *header)
{
  std::string dir = ".";
  if (includer != NULL && includer[0] != '\0')
    {
      std::string path = includer;
      size_t slash = path.find_last_of ('/');
      dir = (slash == std::string::npos) ? "." : path.substr (0, slash);
      if (dir.empty ())
	dir = "/";
    }
  while (dir.size () > 1 && dir[dir.size () - 1] == '/')
    dir.erase (dir.size () - 1);

  std::string h = header;
  size_t hs = h.find_last_of ('/');
  if (hs != std::string::npos)
    h = h.substr (hs + 1);
  size_t dot = h.find_last_of ('.');
  if (dot != std::string::npos)
    h = h.substr (0, dot);

  std::vector<std::string> dirs;
  dirs.push_back (dir);

  const char *env = getenv ("CRUG_FFI_PATH");
  if (env != NULL)
    {
      std::string paths = env;
      size_t start = 0;
      while (start <= paths.size ())
	{
	  size_t sep = paths.find (':', start);
	  std::string one = sep == std::string::npos
			    ? paths.substr (start)
			    : paths.substr (start, sep - start);
	  if (!one.empty ())
	    dirs.push_back (one);
	  if (sep == std::string::npos)
	    break;
	  start = sep + 1;
	}
    }
  dirs.push_back ("/usr/local/share/crug/ffi");
  dirs.push_back ("/opt/crug/share/crug/ffi");

  for (size_t i = 0; i < dirs.size (); i++)
    {
      std::string cand = dirs[i] + "/" + h + ".crugffi";
      if (access (cand.c_str (), R_OK) == 0)
	{
	  parse_ffi_file (cand.c_str (), false);
	  return;
	}
    }
}

static void (*ffi_prev_include) (cpp_reader *, location_t,
				 const unsigned char *, const char *, int,
				 const cpp_token **);

static void
crug_ffi_include_callback (cpp_reader *pfile, location_t line,
			   const unsigned char *dir_name, const char *header,
			   int angle_brackets, const cpp_token **comments)
{
  if (!angle_brackets && !crug_feature_enabled (CRUG_FEAT_STRICT))
    {
      /* 'expand_location' reads the global line table, so the directory of
	 the including file does not need the (opaque here) cpp_reader.  */
      const char *includer = NULL;
      if (line != UNKNOWN_LOCATION)
	includer = LOCATION_FILE (line);
      discover_for_include (includer, header);
    }
  if (ffi_prev_include != NULL)
    ffi_prev_include (pfile, line, dir_name, header, angle_brackets, comments);
}

void
crug_ffi_setup (void)
{
  if (!ffi_option_seen)
    return;
  cpp_callbacks *cb = cpp_get_callbacks (parse_in);
  ffi_prev_include = cb->include;
  cb->include = crug_ffi_include_callback;
}

void
crug_ffi_finish (void)
{
  std::map<std::string, ffi_fn_decl>::iterator it;

  if (ffi_functions.empty ())
    return;
  for (it = ffi_functions.begin (); it != ffi_functions.end (); ++it)
    if (!it->second.resolved)
      (void) resolve_fn (it->second);
}
