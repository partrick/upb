/*
 * upb - a minimalist implementation of protocol buffers.
 *
 * Copyright (c) 2009 Joshua Haberman.  See LICENSE for details.
 *
 * Provides definitions of .proto constructs:
 * - upb_msgdef: describes a "message" construct.
 * - upb_fielddef: describes a message field.
 * - upb_enumdef: describes an enum.
 * (TODO: descriptions of extensions and services).
 *
 * Defs are immutable and reference-counted.  Contexts reference any defs
 * that are the currently in their symbol table.  If an extension is loaded
 * that adds a field to an existing message, a new msgdef is constructed that
 * includes the new field and the old msgdef is unref'd.  The old msgdef will
 * still be ref'd by message (if any) that were constructed with that msgdef.
 *
 * This file contains routines for creating and manipulating the definitions
 * themselves.  To create and manipulate actual messages, see upb_msg.h.
 */

#ifndef UPB_DEF_H_
#define UPB_DEF_H_

#include "upb_atomic.h"
#include "upb_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/* "Base class" for defs; defines common members and functions.  **************/

// All the different kind of defs we support.  These correspond 1:1 with
// declarations in a .proto file.
enum upb_def_type {
  UPB_DEF_MESSAGE,
  UPB_DEF_ENUM,
  UPB_DEF_SERVICE,
  UPB_DEF_EXTENSION,
  // Represented by a string, symbol hasn't been resolved yet.
  UPB_DEF_UNRESOLVED
};

// Common members.
struct upb_def {
  enum upb_def_type type;
  upb_atomic_refcount_t refcount;
  struct upb_string *fqname;  /* Fully-qualified. */
}

void upb_def_init(struct upb_def *def, enum upb_def_type type,
                  struct upb_string *fqname);

/* Field definition. **********************************************************/

// A upb_fielddef describes a single field in a message.  It isn't a full def
// in the sense that it derives from upb_def.  It cannot stand on its own; it
// is either a field of a upb_msgdef or contained inside a upb_extensiondef.
struct upb_fielddef {
  upb_field_type_t type;
  upb_label_t label;
  upb_field_number_t number;
  struct upb_string *name;

  // These are set only when this fielddef is part of a msgdef.
  uint32_t byte_offset;     // Where in a upb_msg to find the data.
  uint16_t field_index;     // Indicates set bit.

  // For the case of an enum or a submessage, points to the def for that type.
  // We own a ref on this def.
  struct upb_def *def;
};

INLINE bool upb_issubmsg(struct upb_fielddef *f) {
  return upb_issubmsgtype(f->type);
}
INLINE bool upb_isstring(struct upb_fielddef *f) {
  return upb_isstringtype(f->type);
}
INLINE bool upb_isarray(struct upb_fielddef *f) {
  return f->label == GOOGLE_PROTOBUF_FIELDDESCRIPTORPROTO_LABEL_REPEATED;
}

INLINE bool upb_field_ismm(struct upb_fielddef *f) {
  return upb_isarray(f) || upb_isstring(f) || upb_issubmsg(f);
}

INLINE bool upb_elem_ismm(struct upb_fielddef *f) {
  return upb_isstring(f) || upb_issubmsg(f);
}

/* Defined iff upb_field_ismm(f). */
INLINE upb_mm_ptrtype upb_field_ptrtype(struct upb_fielddef *f) {
  if(upb_isarray(f)) return UPB_MM_ARR_REF;
  else if(upb_isstring(f)) return UPB_MM_STR_REF;
  else if(upb_issubmsg(f)) return UPB_MM_MSG_REF;
  else return -1;
}

/* Defined iff upb_elem_ismm(f). */
INLINE upb_mm_ptrtype upb_elem_ptrtype(struct upb_fielddef *f) {
  if(upb_isstring(f)) return UPB_MM_STR_REF;
  else if(upb_issubmsg(f)) return UPB_MM_MSG_REF;
  else return -1;
}

// Interfaces for constructing/destroying fielddefs.  These are internal-only.
struct google_protobuf_FieldDescriptorProto;

// Initializes a upb_fielddef from a FieldDescriptorProto.  The caller must
// have previously allocated the upb_fielddef.
void upb_fielddef_init(struct google_protobuf_FieldDescriptorProto *fd,
                       struct upb_fielddef *f);
void upb_fielddef_uninit(struct upb_fielddef *f);

// Sort the given fielddefs in-place, according to what we think is an optimal
// ordering of fields.  This can change from upb release to upb release.
void upb_fielddef_sort(struct upb_fielddef *defs, size_t num);

/* Message definition. ********************************************************/

struct google_protobuf_EnumDescriptorProto;
struct google_protobuf_DescriptorProto;

// Structure that describes a single .proto message type.
struct upb_msgdef {
  struct upb_def def;
  struct upb_msg *default_msg;   // Message with all default values set.
  size_t size;
  uint32_t num_fields;
  uint32_t set_flags_bytes;
  uint32_t num_required_fields;  // Required fields have the lowest set bytemasks.
  struct upb_fielddef *fields;   // We have exclusive ownership of these.

  // Tables for looking up fields by number and name.
  struct upb_inttable fields_by_num;
  struct upb_strtable fields_by_name;
};

// The num->field and name->field maps in upb_msgdef allow fast lookup of fields
// by number or name.  These lookups are in the critical path of parsing and
// field lookup, so they must be as fast as possible.
struct upb_fieldsbynum_entry {
  struct upb_inttable_entry e;
  struct upb_fielddef f;
};
struct upb_fieldsbyname_entry {
  struct upb_strtable_entry e;
  struct upb_fielddef f;
};

// Looks up a field by name or number.  While these are written to be as fast
// as possible, it will still be faster to cache the results of this lookup if
// possible.  These return NULL if no such field is found.
INLINE struct upb_fielddef *upb_msg_fieldbynum(struct upb_msgdef *m,
                                               uint32_t number) {
  struct upb_fieldsbynum_entry *e = upb_inttable_fast_lookup(
      &m->fields_by_num, number, sizeof(struct upb_fieldsbynum_entry));
  return e ? &e->f : NULL;
}

INLINE struct upb_fielddef *upb_msg_fieldbyname(struct upb_msgdef *m,
                                                struct upb_string *name) {
  struct upb_fieldsbyname_entry *e = upb_strtable_lookup(
      &m->fields_by_name, name);
  return e ? &e->f : NULL;
}

/* Enum defintion. ************************************************************/

struct upb_enumdef {
  upb_atomic_refcount_t refcount;
  struct upb_strtable nametoint;
  struct upb_inttable inttoname;
};

struct upb_enumdef_ntoi_entry {
  struct upb_strtable_entry e;
  uint32_t value;
};

struct upb_enumdef_iton_entry {
  struct upb_inttable_entry e;
  struct upb_string *string;
};

/* Internal functions. ********************************************************/

/* Initializes/frees a upb_msgdef.  Usually this will be called by upb_context,
 * and clients will not have to construct one directly.
 *
 * Caller retains ownership of d and fqname.  Note that init does not resolve
 * upb_fielddef.ref the caller should do that post-initialization by
 * calling upb_msg_ref() below.
 *
 * fqname indicates the fully-qualified name of this message.
 *
 * sort indicates whether or not it is safe to reorder the fields from the order
 * they appear in d.  This should be false if code has been compiled against a
 * header for this type that expects the given order. */
void upb_msgdef_init(struct upb_msgdef *m,
                     struct google_protobuf_DescriptorProto *d,
                     struct upb_string *fqname, bool sort,
                     struct upb_status *status);
void _upb_msgdef_free(struct upb_msgdef *m);
INLINE void upb_msgdef_ref(struct upb_msgdef *m) {
  upb_atomic_ref(&m->refcount);
}
INLINE void upb_msgdef_unref(struct upb_msgdef *m) {
  if(upb_atomic_unref(&m->refcount)) _upb_msgdef_free(m);
}

/* Clients use this function on a previously initialized upb_msgdef to resolve
 * the "ref" field in the upb_fielddef.  Since messages can refer to each
 * other in mutually-recursive ways, this step must be separated from
 * initialization. */
void upb_msgdef_setref(struct upb_msgdef *m, struct upb_fielddef *f,
                       union upb_symbol_ref ref);

/* Initializes and frees an enum, respectively.  Caller retains ownership of
 * ed.  The enumdef is initialized with one ref. */
void upb_enumdef_init(struct upb_enumdef *e,
                      struct google_protobuf_EnumDescriptorProto *ed);
void _upb_enumdef_free(struct upb_enumdef *e);
INLINE void upb_enumdef_ref(struct upb_enumdef *e) {
  upb_atomic_ref(&e->refcount);
}
INLINE void upb_enumdef_unref(struct upb_enumdef *e) {
  if(upb_atomic_unref(&e->refcount)) _upb_enumdef_free(e);
}

INLINE void upb_def_ref(struct upb_def *def) { upb_atomic_ref(&def->refcount); }
INLINE void upb_def_unref(struct upb_def *def) {
  if(upb_atomic_unref(&def->refcount)) {
  switch(def->type) {
    case UPB_DEF_MESSAGE:
      _upb_msgdef_free((struct upb_msgdef*)def);
      break;
    case UPB_DEF_ENUM:
      _upb_emumdef_free((struct upb_enumdef*)def);
      break;
    case UPB_DEF_SERVICE:
      assert(false);  /* Unimplemented. */
      break;
    case UPB_DEF_EXTENSION,
      _upb_extensiondef_free((struct upb_extensiondef*)def);
      break;
    case UPB_DEF_UNRESOLVED
      upb_string_unref((struct upb_string*)def);
      break;
    default:
      assert(false);
  }
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* UPB_DEF_H_ */