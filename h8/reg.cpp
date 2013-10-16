/*
 *      Interactive disassembler (IDA).
 *      Copyright (c) 1990-2001 by Ilfak Guilfanov.
 *      ALL RIGHTS RESERVED.
 *                              FIDO:   2:5020/209
 *                              E-mail: ig@datarescue.com
 *
 */

#include "h8.hpp"
#include <diskio.hpp>
#include <typeinf.hpp>

#include <ieee.h>

//--------------------------------------------------------------------------
static const char *const register_names[] =
{
  "r0",   "r1",   "r2",  "r3",  "r4",  "r5",  "r6",  "sp",
  "e0",   "e1",   "e2",  "e3",  "e4",  "e5",  "e6",  "e7",
  "r0h",  "r1h",  "r2h", "r3h", "r4h", "r5h", "r6h", "r7h",
  "r0l",  "r1l",  "r2l", "r3l", "r4l", "r5l", "r6l", "r7l",
  "er0",  "er1",  "er2", "er3", "er4", "er5", "er6", "sp",
  "macl", "mach",
  "pc",
  "ccr",  "exr",
  "cs","ds",       // virtual registers for code and data segments
};

//--------------------------------------------------------------------------
static const uchar retcode_0[] = { 0x56, 0x70 };  // rte
static const uchar retcode_1[] = { 0x54, 0x70 };  // rts

static const bytes_t retcodes[] =
{
  { sizeof(retcode_0), retcode_0 },
  { sizeof(retcode_1), retcode_1 },
  { 0, NULL }
};

//-----------------------------------------------------------------------
//      GNU ASM
//-----------------------------------------------------------------------
static const asm_t gas =
{
  AS_ASCIIC|AS_ALIGN2|ASH_HEXF3|ASD_DECF0|ASB_BINF3|ASO_OCTF1|AS_COLON|AS_N2CHR|AS_NCMAS|AS_ONEDUP,
  0,
  "GNU assembler",
  0,
  NULL,         // header lines
  NULL,         // no bad instructions
  ".org",       // org
  NULL,         // end

  ";",          // comment string
  '"',          // string delimiter
  '"',          // char delimiter
  "\"",         // special symbols in char and string constants

  ".ascii",     // ascii string directive
  ".byte",      // byte directive
  ".word",      // word directive
  ".long",      // double words
  NULL,         // qwords
  NULL,         // oword  (16 bytes)
  ".float",     // float  (4 bytes)
  ".double",    // double (8 bytes)
  NULL,         // tbyte  (10/12 bytes)
  NULL,         // packed decimal real
  NULL,         // arrays (#h,#d,#v,#s(...)
  ".space %s",  // uninited arrays
  "=",          // equ
  NULL,         // 'seg' prefix (example: push seg seg001)
  NULL,         // Pointer to checkarg_preline() function.
  NULL,         // char *(*checkarg_atomprefix)(char *operand,void *res); // if !NULL, is called before each atom
  NULL,         // const char **checkarg_operations;
  NULL,         // translation to use in character and string constants.
  NULL,         // current IP (instruction pointer)
  NULL,         // func_header
  NULL,         // func_footer
  ".globl",     // "public" name keyword
  NULL,         // "weak"   name keyword
  ".extern",    // "extrn"  name keyword
                // .extern directive requires an explicit object size
  ".comm",      // "comm" (communal variable)
  NULL,         // get_type_name
  ".align",     // "align" keyword
  '(', ')',     // lbrace, rbrace
  "%",          // mod
  "&",          // and
  "|",          // or
  "^",          // xor
  "~",          // not
  "<<",         // shl
  ">>",         // shr
  NULL,         // sizeof_fmt
  0,            // flag2
  NULL,         // cmnt2
  NULL,         // low8
  NULL,         // high8
  NULL,         // low16
  NULL,         // high16
  "#include \"%s\"",  // a_include_fmt
};

static const asm_t *const asms[] = { &gas, NULL };

//--------------------------------------------------------------------------
static char device[MAXSTR] = "";
static ioport_t *ports = NULL;
static size_t numports = 0;
static const char cfgname[] = "h8.cfg";

static void load_symbols(void)
{
  free_ioports(ports, numports);
  ports = read_ioports(&numports, cfgname, device, sizeof(device), NULL);
}

//--------------------------------------------------------------------------
const char *find_sym(ea_t address)
{
  const ioport_t *port = find_ioport(ports, numports, address);
  return port ? port->name : NULL;
}

//--------------------------------------------------------------------------
const char *idaapi set_idp_options(const char *keyword,int /*value_type*/,const void * /*value*/)
{
  if ( keyword != NULL ) return IDPOPT_BADKEY;
  if ( choose_ioport_device(cfgname, device, sizeof(device), NULL) )
    load_symbols();
  return IDPOPT_OK;
}

//--------------------------------------------------------------------------
netnode helper;
proctype_t ptype;

static const proctype_t ptypes[] =
{
        P300,
  ADV | P300,
        P300 | P2000 | P2600,
  ADV | P300 | P2000 | P2600,
};


static int idaapi notify(processor_t::idp_notify msgid, ...)
{
  va_list va;
  va_start(va, msgid);

// A well behaving processor module should call invoke_callbacks()
// in his notify() function. If this function returns 0, then
// the processor module should process the notification itself
// Otherwise the code should be returned to the caller:

  int code = invoke_callbacks(HT_IDP, msgid, va);
  if ( code ) return code;

  switch ( msgid )
  {
    case processor_t::init:
//      __emit__(0xCC);   // debugger trap
      helper.create("$ h8");
      helper.supval(0, device, sizeof(device));
      inf.mf = 1;
    default:
      break;

    /* +++ START TYPEINFO CALLBACKS +++ */
    // see module/{i960,hppa}/reg.cpp starting on line 253
    // Decorate/undecorate a C symbol name
    // Arguments:
    // const til_t *ti    - pointer to til
    // const char *name   - name of symbol
    // const type_t *type - type of symbol. If NULL then it will try to guess.
    // char *outbuf       - output buffer
    // size_t bufsize     - size of the output buffer
    // bool mangle        - true-mangle, false-unmangle
    // cm_t cc            - real calling convention for VOIDARG functions
    // returns: true if success
    case processor_t::decorate_name:
      {
        const til_t *ti    = va_arg(va, const til_t *);
        const char *name   = va_arg(va, const char *);
        const type_t *type = va_arg(va, const type_t *);
        char *outbuf       = va_arg(va, char *);
        size_t bufsize     = va_arg(va, size_t);
        bool mangle        = va_argi(va, bool);
        cm_t real_cc       = va_argi(va, cm_t);
        return gen_decorate_name(ti, name, type, outbuf, bufsize, mangle, real_cc);
      }

    // Setup default type libraries (called after loading a new file into the database)
    // The processor module may load tils, setup memory model and perform other actions
    // required to set up the type system.
    // args:    none
    // returns: nothing
    case processor_t::setup_til:
      {
      }

    // Purpose: get prefix and size of 'segment based' ptr type (something like
    // char _ss *ptr). See description in typeinf.hpp.
    // Other modules simply set the pointer to NULL and return 0
    // Ilfak confirmed that this approach is correct for the H8.
    // Used only for BTMT_CLOSURE types, doubtful you will encounter them for H8.
    // Arguments:
    // unsigned int ptrt    - ...
    // const char **ptrname - output arg
    // returns: size of type
    case processor_t::based_ptr:
      {
        /*unsigned int ptrt =*/ va_arg(va, unsigned int);
        char **ptrname    = va_arg(va, char **);
        *ptrname = NULL;
        return 0;
      }

    // The H8 supports normal (64KB addressing, 16 bits) and advanced mode
    // (16MB addressing, 24 bits).  However, according to the Renesas technical
    // documentation, certain instructions accept 32-bit pointer values where
    // the upper 8 bits are "reserved".  Ilfak confirms that "4+1" is fine.
    // Used only for BTMT_CLOSURE types, doubtful you will encounter them for H8.
    case processor_t::max_ptr_size:
      {
        return 4+1;
      }

    // get default enum size
    // args:  cm_t cm
    // returns: sizeof(enum)
    case processor_t::get_default_enum_size:
      {
        // cm_t cm        =  va_argi(va, cm_t);
        return inf.cc.size_e;
      }

    case processor_t::use_stkarg_type:
      {
        ea_t ea            = va_arg(va, ea_t);
        const type_t *type = va_arg(va, const type_t *);
        const char *name   = va_arg(va, const char *);
        return h8_use_stkvar_type(ea, type, name);
      }

    // calculate number of purged bytes by the given function type
    // For cdecl functions, 'purged bytes' is always zero
    // See the IDA Pro Book, 2e, at the end of pp. 107 for details
    // args: type_t *type - must be function type
    // returns: number of bytes purged from the stack + 2
    case processor_t::calc_purged_bytes:
      {
        //e.g. const type_t t_int[] = { BT_INT, 0 };
        //const type_t *type = va_arg(va, const type_t *); // must be BT_FUNC
        return 0+2;
      }

    case processor_t::calc_arglocs2:
      {
        const type_t *type = va_arg(va, const type_t *);
        cm_t cc            = va_argi(va, cm_t);
        varloc_t *arglocs  = va_arg(va, varloc_t *);
        return h8_calc_arglocs(type, cc, arglocs);
      }

    /* +++ END TYPEINFO CALLBACKS +++ */

    case processor_t::term:
      free_ioports(ports, numports);
      break;

    case processor_t::newfile:   // new file loaded
      load_symbols();
      break;

    case processor_t::oldfile:   // old file loaded
      load_symbols();
      break;

    case processor_t::closebase:
    case processor_t::savebase:
      helper.supset(0, device);
      break;

    case processor_t::newprc:    // new processor type
      ptype = ptypes[va_arg(va, int)];
      setflag(ph.flag, PR_DEFSEG32, ptype & ADV);
      break;

    case processor_t::newasm:    // new assembler type
      break;

    case processor_t::newseg:    // new segment
      break;

    case processor_t::is_jump_func:
      {
        const func_t *pfn = va_arg(va, const func_t *);
        ea_t *jump_target = va_arg(va, ea_t *);
        return is_jump_func(pfn, jump_target);
      }

    case processor_t::is_sane_insn:
      return is_sane_insn(va_arg(va, int));

    case processor_t::may_be_func:
                                // can a function start here?
                                // arg: none, the instruction is in 'cmd'
                                // returns: probability 0..100
                                // 'cmd' structure is filled upon the entrace
                                // the idp module is allowed to modify 'cmd'
      return may_be_func();

  }
  va_end(va);
  return 1;
}

//-----------------------------------------------------------------------
static const char *const shnames[] = { "h8300", "h8300a", "h8s300", "h8s300a", NULL };
static const char *const lnames[] =
{
  "Hitachi H8/300H normal",
  "Hitachi H8/300H advanced",
  "Hitachi H8S normal",
  "Hitachi H8S advanced",
  NULL
};

//-----------------------------------------------------------------------
//      Processor Definition
//-----------------------------------------------------------------------
processor_t LPH =
{
  IDP_INTERFACE_VERSION,        // version
  PLFM_H8,                      // id
  PRN_HEX | PR_USE32 | PR_TYPEINFO,
  8,                            // 8 bits in a byte for code segments
  8,                            // 8 bits in a byte for other segments

  shnames,
  lnames,

  asms,

  notify,

  header,
  footer,

  segstart,
  segend,

  NULL,                 // generate "assume" directives

  ana,                  // analyze instruction
  emu,                  // emulate instruction

  out,                  // generate text representation of instruction
  outop,                // generate ...                    operand
  intel_data,           // generate ...                    data directive
  NULL,                 // compare operands
  NULL,                 // can have type

  qnumber(register_names), // Number of registers
  register_names,       // Register names
  NULL,                 // get abstract register

  0,                    // Number of register files
  NULL,                 // Register file names
  NULL,                 // Register descriptions
  NULL,                 // Pointer to CPU registers

  rVcs,                 // first
  rVds,                 // last
  0,                    // size of a segment register
  rVcs, rVds,

  NULL,                 // No known code start sequences
  retcodes,

  H8_null,
  H8_last,
  Instructions,

  NULL,                 // int  (*is_far_jump)(int icode);
  NULL,                 // Translation function for offsets
  0,                    // int tbyte_size;  -- doesn't exist
  ieee_realcvt,         // int (*realcvt)(void *m, ushort *e, ushort swt);
  { 0, 7, 15, 0 },      // char real_width[4];
                        // number of symbols after decimal point
                        // 2byte float (0-does not exist)
                        // normal float
                        // normal double
                        // long double
  NULL,                 // int (*is_switch)(switch_info_t *si);
  NULL,                 // int32 (*gen_map_file)(FILE *fp);
  NULL,                 // ea_t (*extract_address)(ea_t ea,const char *string,int x);
  is_sp_based,          // int (*is_sp_based)(op_t &x);
  create_func_frame,    // int (*create_func_frame)(func_t *pfn);
  h8_get_frame_retsize, // int (*get_frame_retsize(func_t *pfn)
  NULL,                 // void (*gen_stkvar_def)(char *buf,const member_t *mptr,long v);
  gen_spcdef,           // Generate text representation of an item in a special segment
  H8_rts,               // Icode of return instruction. It is ok to give any of possible return instructions
  set_idp_options,      // const char *(*set_idp_options)(const char *keyword,int value_type,const void *value);
  is_align_insn,        // int (*is_align_insn)(ea_t ea);
  NULL,                 // mvm_t *mvm;
};
