/* Copyright (C) 2026 Free Software Foundation, Inc.

   This file is part of the GNU Offloading and Multi Processing Library
   (libgomp).

   Libgomp is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   Libgomp is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   Under Section 7 of GPL version 3, you are granted additional
   permissions described in the GCC Runtime Library Exception, version
   3.1, as published by the Free Software Foundation.

   You should have received a copy of the GNU General Public License and
   a copy of the GCC Runtime Library Exception along with this program;
   see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
   <http://www.gnu.org/licenses/>.  */

/* Internal hash interface for the experimental transaction runtime.
   Trace-only skeleton: calls print arguments, status functions return INVALID,
   output handles are null and value queries return null/zero.
   The temporary compiler-facing bridge uses this interface only for tracing.  */

#ifndef LIBGOMP_NEWTM_HASH_H
#define LIBGOMP_NEWTM_HASH_H

#include <stddef.h>

#define NEWTM_HASH_DEFAULT_BUCKETS 1024

struct newtm_hash_table;
struct newtm_hash_entry;

enum newtm_hash_status
{
  NEWTM_HASH_OK,
  NEWTM_HASH_INVALID,
  NEWTM_HASH_NOT_FOUND,
  NEWTM_HASH_SIZE_MISMATCH,
  NEWTM_HASH_BUSY,
  NEWTM_HASH_OVERFLOW
};

/* Called outside the bucket lock when the last reference is released.
   Owns and destroys POLICY_DATA; must not reenter the table.  */
typedef void (*newtm_hash_policy_destroy_fn) (void *policy_data);

/* Called under the bucket lock.  May update *POLICY_DATA; ARGUMENT carries
   caller input/output.  Must not reenter the table or execute user code.  */
typedef void (*newtm_hash_visit_fn) (const void *address, size_t size,
                                   void **policy_data, void *argument);

/* Create an empty table with BUCKET_COUNT buckets and one mutex per bucket.
   DESTROY_POLICY owns nonnull entry metadata; it may be null only when all
   metadata remain null.  Store the new table in *OUT.  Return OK, INVALID
   for zero capacity/null OUT, or OVERFLOW for an unrepresentable allocation.
   On failure clear *OUT when OUT is valid.  Allocation failure is fatal.  */
extern enum newtm_hash_status newtm_hash_create (size_t bucket_count,
                   newtm_hash_policy_destroy_fn destroy_policy,
                   struct newtm_hash_table **out)
  __attribute__ ((visibility ("hidden")));

/* With all users quiescent, destroy TABLE and its bucket mutexes if empty.
   Return INVALID for null TABLE, BUSY if any entry remains, or OK on success.
   BUSY must leave the complete table intact; success invalidates TABLE.  */
extern enum newtm_hash_status newtm_hash_destroy (struct newtm_hash_table *table)
  __attribute__ ((visibility ("hidden")));

/* Find or insert ADDRESS with SIZE bytes in TABLE under its bucket mutex.
   Return a retained entry in *OUT, creating it with null metadata if absent.
   Return OK, INVALID for null arguments/zero SIZE, SIZE_MISMATCH for an
   existing key with another size, or OVERFLOW for counters/allocation size.
   Clear *OUT on failure when valid; make no partial changes.  Allocation
   failure is fatal.  Do not read or reserve the program object.  */
extern enum newtm_hash_status newtm_hash_acquire (struct newtm_hash_table *table, const void *address,
                    size_t size, struct newtm_hash_entry **out)
  __attribute__ ((visibility ("hidden")));

/* Look up ADDRESS without insertion and retain the entry under its bucket
   mutex.  Return OK, NOT_FOUND, INVALID for null arguments, or OVERFLOW for
   the reference counter.  Clear *OUT on failure when valid.  The caller must
   release a successful reference; it is not a transactional reservation.  */
extern enum newtm_hash_status newtm_hash_lookup_acquire (struct newtm_hash_table *table,
                           const void *address,
                           struct newtm_hash_entry **out)
  __attribute__ ((visibility ("hidden")));

/* Release one caller-owned reference to ENTRY in TABLE under its bucket lock.
   Unlink at zero references, then unlock, destroy metadata and free the entry.
   Require valid matching TABLE/ENTRY and reservations already withdrawn.
   Return no value.  ENTRY may become invalid; never force removal while used.  */
extern void newtm_hash_release (struct newtm_hash_table *table,
                    struct newtm_hash_entry *entry)
  __attribute__ ((visibility ("hidden")));

/* Return the immutable canonical program-object address stored in ENTRY.
   Require a valid retained reference; no lock or object dereference is needed.
   The returned address does not extend the lifetime of the program object.  */
extern const void * newtm_hash_entry_address (const struct newtm_hash_entry *entry)
  __attribute__ ((visibility ("hidden")));

/* Return ENTRY's immutable object size in bytes.  Require a valid retained
   reference; do not read the object or acquire a transactional reservation.  */
extern size_t newtm_hash_entry_size (const struct newtm_hash_entry *entry)
  __attribute__ ((visibility ("hidden")));

/* Call nonnull VISIT on the address, size and metadata slot of retained ENTRY
   under its bucket mutex in TABLE, forwarding ARGUMENT (which may be null).
   Require valid matching TABLE/ENTRY.  Return no value; results travel through
   ARGUMENT.  Do not retain the lock after the callback or allow reentrancy.  */
extern void newtm_hash_entry_visit (struct newtm_hash_table *table,
                        struct newtm_hash_entry *entry,
                        newtm_hash_visit_fn visit, void *argument)
  __attribute__ ((visibility ("hidden")));

/* Return a consistent entry count, saturated at SIZE_MAX, for valid TABLE.
   Lock every bucket in ascending order, sum counts, unlock in reverse order.
   For diagnostics only; never call from a visit callback.  References and
   transactional participants are not counted.  */
extern size_t newtm_hash_count (struct newtm_hash_table *table)
  __attribute__ ((visibility ("hidden")));

#endif /* LIBGOMP_NEWTM_HASH_H */
