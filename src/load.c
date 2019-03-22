/*! @file
  @brief
  mruby bytecode loader.

  <pre>
  Copyright (C) 2015-2018 Kyushu Institute of Technology.
  Copyright (C) 2015-2018 Shimane IT Open-Innovation Center.

  This file is distributed under BSD 3-Clause License.

  </pre>
*/

#include "vm_config.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "vm.h"
#include "load.h"
#include "value.h"
#include "alloc.h"


//================================================================
/*! Parse header section.

  @param  mrbbuf	pointer to mrb file buffer.
  @return int		header size.

  <pre>
  Structure
   "RITE"	identifier
   "0004"	version
   0000		CRC
   0000_0000	total size
   "MATZ"	compiler name
   "0000"	compiler version
  </pre>
*/
static int check_file_header(const uint8_t *mrbbuf)
{
  static const char RITE0004[8] = "RITE0004";
  static const char MATZ[4] = "MATZ";
  static const char A0000[4] = "0000";

  if( memcmp(mrbbuf, RITE0004, sizeof(RITE0004)) != 0 ) return -1;

  /* Ignore CRC */

  int size = bin_to_uint32(mrbbuf + 10);

  if( memcmp(mrbbuf + 14, MATZ, sizeof(MATZ)) != 0 ) return -1;
  if( memcmp(mrbbuf + 18, A0000, sizeof(A0000)) != 0 ) return -1;

  return size;
}


//================================================================
/*! Load irep one segment (ISEQ, POOL and SYMS).

  @param  pp_irep	return variable of allocated mrbc_irep.
  @param  mrbbuf	pointer to mrb file buffer.
  @param  idx		start index of mrbbuf.

  @return int		parse end index. negative value is error.

  <pre>
   (loop n of child irep bellow)
   0000_0000	record size
   0000		n of local variable
   0000		n of register
   0000		n of child irep

   0000_0000	n of byte code  (ISEQ BLOCK)
   ...		byte codes

   0000_0000	n of pool	(POOL BLOCK)
   (loop n of pool)
     00		type
     0000	length
     ...	pool data

   0000_0000	n of symbol	(SYMS BLOCK)
   (loop n of symbol)
     0000	length
     ...	symbol data
  </pre>
*/
static int load_irep_1(mrbc_irep **pp_irep, const uint8_t *mrbbuf, int idx)
{
  const uint8_t *p = mrbbuf + idx + 4;		// 4 = skip record size

  // new irep
  mrbc_irep *irep = mrbc_irep_alloc(0);
  if( irep == NULL ) return -E_NOMEMORY_ERROR;

  // nlocals,nregs,rlen
  irep->nlocals = bin_to_uint16(p);	p += 2;
  irep->nregs = bin_to_uint16(p);	p += 2;
  irep->rlen = bin_to_uint16(p);	p += 2;
  irep->ilen = bin_to_uint32(p);	p += 4;

  // padding
  p += (mrbbuf - p) & 0x03;

  // allocate memory for child irep's pointers
  if( irep->rlen ) {
    irep->reps = (mrbc_irep **)mrbc_alloc(0, sizeof(mrbc_irep *) * irep->rlen);
    if( irep->reps == NULL ) return -E_NOMEMORY_ERROR;
  }

  // ISEQ (code) BLOCK
  irep->code = (uint8_t *)p;
  p += irep->ilen * 4;

  // POOL BLOCK
  irep->plen = bin_to_uint32(p);	p += 4;
  if( irep->plen ) {
    irep->pools = (mrbc_object**)mrbc_alloc(0, sizeof(void*) * irep->plen);
    if( irep->pools == NULL ) return -E_NOMEMORY_ERROR;
  }

  int i;
  for( i = 0; i < irep->plen; i++ ) {
    int tt = *p++;
    int obj_size = bin_to_uint16(p);	p += 2;
    mrbc_object *obj = mrbc_alloc(0, sizeof(mrbc_object));
    if( obj == NULL ) return -E_NOMEMORY_ERROR;

    switch( tt ) {
#if MRBC_USE_STRING
    case 0: { // IREP_TT_STRING
      obj->tt = MRBC_TT_STRING;
      obj->str = (char*)p;
    } break;
#endif
    case 1: { // IREP_TT_FIXNUM
      char buf[obj_size+1];
      memcpy(buf, p, obj_size);
      buf[obj_size] = '\0';
      obj->tt = MRBC_TT_FIXNUM;
      obj->i = atol(buf);
    } break;
#if MRBC_USE_FLOAT
    case 2: { // IREP_TT_FLOAT
      char buf[obj_size+1];
      memcpy(buf, p, obj_size);
      buf[obj_size] = '\0';
      obj->tt = MRBC_TT_FLOAT;
      obj->d = atof(buf);
    } break;
#endif
    default:
      assert(!"Unknown tt");
    }

    irep->pools[i] = obj;
    p += obj_size;
  }

  // SYMS BLOCK
  irep->ptr_to_sym = (uint8_t*)p;
  int slen = bin_to_uint32(p);		p += 4;
  while( --slen >= 0 ) {
    int s = bin_to_uint16(p);		p += 2;
    p += s+1;
  }

  *pp_irep = irep;
  return p - mrbbuf;
}


//================================================================
/*! Load all IREP section.

  @param  pp_irep	return variable of allocated mrbc_irep.
  @param  mrbbuf	pointer to mrb file buffer.
  @param  idx		start index of mrbbuf.

  @return int		parse end index. negative value is error.
*/
static int load_irep_0(mrbc_irep **pp_irep, const uint8_t *mrbbuf, int idx)
{
  idx = load_irep_1(pp_irep, mrbbuf, idx);
  if( idx < 0 ) return idx;

  int i;
  for( i = 0; i < (*pp_irep)->rlen; i++ ) {
    idx = load_irep_0( &((*pp_irep)->reps[i]), mrbbuf, idx );
    if( idx < 0 ) return idx;
  }

  return idx;
}


//================================================================
/*! Parse IREP section and load.

  @param  pp_irep	return variable of allocated mrbc_irep.
  @param  mrbbuf	pointer to mrb file buffer.
  @param  idx		start index of mrbbuf.

  @return int		0 is no error. negative value is error.

  <pre>
  Structure
   "IREP"	section identifier
   0000_0000	section size
   "0000"	rite version
  </pre>
*/
static int load_irep(mrbc_irep **pp_irep, const uint8_t *mrbbuf, int idx)
{
  static const char A0000[4] = "0000";	// RITE version

  const uint8_t *p = mrbbuf + idx;
  if( memcmp(p + 8, A0000, sizeof(A0000)) != 0 ) return -E_TYPE_ERROR;

  int ret = load_irep_0(pp_irep, mrbbuf, idx + 12);
  if( ret < 0 ) return ret;

  return 0;
}


//================================================================
/*! Parse mrb file.

  @param  pp_irep	return variable of allocated mrbc_irep.
  @param  mrbbuf	pointer to mrb file buffer.
  @return int		error code.
*/
int mrbc_parse_mrb(struct IREP **pp_irep, const uint8_t *mrbbuf)
{
  static const char IREP[4] = "IREP";
  static const char END[4]  = "END";

  mrbc_irep *irep = NULL;

  int size = check_file_header(mrbbuf);
  if( size < 0 ) return E_TYPE_ERROR;
  int idx = 22;		// 22: size of file header.

  while( 1 ) {
    const uint8_t *p = mrbbuf + idx;
    if( memcmp(p, END, sizeof(END)) == 0 ) break;

    if( memcmp(p, IREP, sizeof(IREP)) == 0 ) {
      int ret = load_irep(&irep, mrbbuf, idx);
      if( ret < 0 ) return -ret;	// error return.
    }

    idx += bin_to_uint32(p + 4);	// next section.
  }

  if( pp_irep != NULL ) *pp_irep = irep;

  return 0;
}


//================================================================
/*! attach irep to vm

  @param  vm	Pointer to VM.
  @param  irep	Pointer to IREP.

*/
void mrbc_attach_irep(struct VM *vm, struct IREP *irep )
{
  vm->irep = irep;
}


//================================================================
/*! Load the VM bytecode.

  @param  vm		Pointer to VM.
  @param  mrbbuf	Pointer to bytecode.

*/
int mrbc_load_mrb(struct VM *vm, const uint8_t *mrbbuf)
{
  mrbc_irep *irep = NULL;
  int ret = mrbc_parse_mrb(&irep, mrbbuf);
  if( ret == 0 ) mrbc_attach_irep( vm, irep );

  return ret;
}
