/* .crugffi v1 metadata: parser, merge and validation (ADR 0007).  */

#ifndef GCC_C_FAMILY_CRUG_FFI_H
#define GCC_C_FAMILY_CRUG_FFI_H

#include <string>
#include <vector>

/* Resolved contract of one annotated function.  Built lazily the first
   time the ownership pass looks at a call, and again for every remaining
   entry when parsing finishes, so that an explicit sidecar which names a
   function the translation unit does not declare is still diagnosed.  */

struct crug_ffi_info
{
  tree fndecl;
  bool returns_owned;
  bool returns_nullable;
  tree owned_type;
  tree dtor;
  std::vector<bool> consumes;
  std::vector<bool> borrows;
  std::vector<bool> mut_borrows;
  std::vector<bool> out_owned;
  std::vector<bool> callback;
  std::vector<bool> nullable;
  std::vector<tree> out_dtor;
  /* 'thread_affinity NAME': the call returns a value that belongs to one
     thread and may not migrate to a task or a channel (ADR 0007, ADR 0026
     D7).  */
  bool thread_affine;
  std::string affinity;
};

/* True once -fcrug-ffi=<path> was given.  */
extern bool crug_ffi_enabled_p (void);

/* Handle one -fcrug-ffi=<path> occurrence.  The path is recorded and parsed
   once, before the first contract query or when parsing ends.  */
extern void crug_ffi_handle_option (const char *path);

/* -fcrug-ffi-discover: turn the layer on without naming a file, so that
   sidecars next to quoted includes are discovered.  */
extern void crug_ffi_enable_discovery (void);

/* Install the quoted-include discovery callback.  Called once the
   preprocessor exists.  */
extern void crug_ffi_setup (void);

/* Validate every entry against the translation unit and release the
   parsing state.  Called when parsing ends.  */
extern void crug_ffi_finish (void);

/* The contract of FNDECL, or NULL when unannotated.  */
extern const struct crug_ffi_info *crug_ffi_lookup (tree fndecl);

/* Convenience accessors for the ownership pass.  */
extern bool crug_ffi_returns_owned (tree fndecl, tree *dtor);
extern bool crug_ffi_arg_consumes (tree fndecl, unsigned int idx);
extern bool crug_ffi_arg_out_owned (tree fndecl, unsigned int idx, tree *dtor);
extern bool crug_ffi_arg_callback (tree fndecl, unsigned int idx);
extern bool crug_ffi_arg_borrow (tree fndecl, unsigned int idx, bool *mut_p);
extern bool crug_ffi_arg_nullable (tree fndecl, unsigned int idx);

/* The thread affinity of FNDECL, when its metadata carries one.  */
extern bool crug_ffi_thread_affine (tree fndecl, const char **name);

/* The explicit send/sync annotations of TYPE, when the sidecar has any.
   Returns false when TYPE is not named by the metadata at all.  */
extern bool crug_ffi_type_flags (tree type, bool *send, bool *sync,
				 bool *not_send, bool *not_sync);

#endif /* GCC_C_FAMILY_CRUG_FFI_H */
