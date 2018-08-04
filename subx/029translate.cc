//: The bedrock level 1 of abstraction is now done, and we're going to start
//: building levels above it that make programming in x86 machine code a
//: little more ergonomic.
//:
//: All levels will be "pass through by default". Whatever they don't
//: understand they will silently pass through to lower levels.
//:
//: Since raw hex bytes of machine code are always possible to inject, SubX is
//: not a language, and we aren't building a compiler. This is something
//: deliberately leakier. Levels are more for improving auditing, checks and
//: error messages rather than for hiding low-level details.

//: Translator workflow: read 'source' file. Run a series of transforms on it,
//: each passing through what it doesn't understand. The final program should
//: be just machine code, suitable to write to an ELF binary.
//:
//: Higher levels usually transform code on the basis of metadata.

:(before "End Main")
if (is_equal(argv[1], "translate")) {
  START_TRACING_UNTIL_END_OF_SCOPE;
  assert(argc > 3);
  program p;
  ifstream fin(argv[2]);
  if (!fin) {
    cerr << "could not open " << argv[2] << '\n';
    return 1;
  }
  parse(fin, p);
  if (trace_contains_errors()) return 1;
  transform(p);
  if (trace_contains_errors()) return 1;
  save_elf(p, argv[3]);
  if (trace_contains_errors()) unlink(argv[3]);
  return 0;
}

//: Ordering transforms is a well-known hard problem when building compilers.
//: In our case we also have the additional notion of layers. The ordering of
//: layers can have nothing in common with the ordering of transforms when
//: SubX is tangled and run. This can be confusing for readers, particularly
//: if later layers start inserting transforms at arbitrary points between
//: transforms introduced earlier. Over time adding transforms can get harder
//: and harder, having to meet the constraints of everything that's come
//: before. It's worth thinking about organization up-front so the ordering is
//: easy to hold in our heads, and it's obvious where to add a new transform.
//: Some constraints:
//:
//:   1. Layers force us to build SubX bottom-up; since we want to be able to
//:   build and run SubX after stopping loading at any layer, the overall
//:   organization has to be to introduce primitives before we start using
//:   them.
//:
//:   2. Transforms usually need to be run top-down, converting high-level
//:   representations to low-level ones so that low-level layers can be
//:   oblivious to them.
//:
//:   3. When running we'd often like new representations to be checked before
//:   they are transformed away. The whole reason for new representations is
//:   often to add new kinds of automatic checking for our machine code
//:   programs.
//:
//: Putting these constraints together, we'll use the following broad
//: organization:
//:
//:   a) We'll divide up our transforms into "levels", each level consisting
//:   of multiple transforms, and dealing in some new set of representational
//:   ideas. Levels will be added in reverse order to the one their transforms
//:   will be run in.
//:
//:     To run all transforms:
//:       Load transforms for level n
//:       Load transforms for level n-1
//:       ...
//:       Load transforms for level 2
//:       Run code at level 1
//:
//:   b) *Within* a level we'll usually introduce transforms in the order
//:   they're run in.
//:
//:     To run transforms for level n:
//:       Perform transform of layer l
//:       Perform transform of layer l+1
//:       ...
//:
//:   c) Within a level it's often most natural to introduce a new
//:   representation by showing how it's transformed to the level below. To
//:   make such exceptions more obvious checks usually won't be first-class
//:   transforms; instead code that keeps the program unmodified will run
//:   within transforms before they mutate the program.
//:
//:     Level l transforms programs
//:     Level l+1 inserts checks to run *before* the transform of level l runs
//:
//: This may all seem abstract, but will hopefully make sense over time. The
//: goals are basically to always have a working program after any layer, to
//: have the order of layers make narrative sense, and to order transforms
//: correctly at runtime.
:(before "End One-time Setup")
// Begin Transforms
// End Transforms

:(code)
// write out a program to a bare-bones ELF file
void save_elf(const program& p, const char* filename) {
  ofstream out(filename, ios::binary);
  write_elf_header(out, p);
  for (size_t i = 0;  i < p.segments.size();  ++i)
    write_segment(p.segments.at(i), out);
  out.close();
}

void write_elf_header(ostream& out, const program& p) {
  char c = '\0';
#define O(X)  c = (X); out.write(&c, sizeof(c))
// host is required to be little-endian
#define emit(X)  out.write(reinterpret_cast<const char*>(&X), sizeof(X))
  //// ehdr
  // e_ident
  O(0x7f); O(/*E*/0x45); O(/*L*/0x4c); O(/*F*/0x46);
    O(0x1);  // 32-bit format
    O(0x1);  // little-endian
    O(0x1); O(0x0);
  for (size_t i = 0;  i < 8;  ++i) { O(0x0); }
  // e_type
  O(0x02); O(0x00);
  // e_machine
  O(0x03); O(0x00);
  // e_version
  O(0x01); O(0x00); O(0x00); O(0x00);
  // e_entry
  int e_entry = p.segments.at(0).start;  // convention
  emit(e_entry);
  // e_phoff -- immediately after ELF header
  int e_phoff = 0x34;
  emit(e_phoff);
  // e_shoff; unused
  int dummy32 = 0;
  emit(dummy32);
  // e_flags; unused
  emit(dummy32);
  // e_ehsize
  uint16_t e_ehsize = 0x34;
  emit(e_ehsize);
  // e_phentsize
  uint16_t e_phentsize = 0x20;
  emit(e_phentsize);
  // e_phnum
  uint16_t e_phnum = SIZE(p.segments);
  emit(e_phnum);
  // e_shentsize
  uint16_t dummy16 = 0x0;
  emit(dummy16);
  // e_shnum
  emit(dummy16);
  // e_shstrndx
  emit(dummy16);

  uint32_t p_offset = /*size of ehdr*/0x34 + SIZE(p.segments)*0x20/*size of each phdr*/;
  for (int i = 0;  i < SIZE(p.segments);  ++i) {
    //// phdr
    // p_type
    uint32_t p_type = 0x1;
    emit(p_type);
    // p_offset
    emit(p_offset);
    // p_vaddr
    emit(p.segments.at(i).start);
    // p_paddr
    emit(p.segments.at(i).start);
    // p_filesz
    uint32_t size = size_of(p.segments.at(i));
    assert(size < SEGMENT_SIZE);
    emit(size);
    // p_memsz
    emit(size);
    // p_flags
    uint32_t p_flags = (i == 0) ? /*r-x*/0x5 : /*rw-*/0x6;  // convention: only first segment is code
    emit(p_flags);

    // p_align
    // "As the system creates or augments a process image, it logically copies
    // a file's segment to a virtual memory segment.  When—and if— the system
    // physically reads the file depends on the program's execution behavior,
    // system load, and so on.  A process does not require a physical page
    // unless it references the logical page during execution, and processes
    // commonly leave many pages unreferenced. Therefore delaying physical
    // reads frequently obviates them, improving system performance. To obtain
    // this efficiency in practice, executable and shared object files must
    // have segment images whose file offsets and virtual addresses are
    // congruent, modulo the page size." -- http://refspecs.linuxbase.org/elf/elf.pdf (page 95)
    uint32_t p_align = 0x1000;  // default page size on linux
    emit(p_align);
    if (p_offset % p_align != p.segments.at(i).start % p_align) {
      raise << "segment starting at 0x" << HEXWORD << p.segments.at(i).start << " is improperly aligned; alignment for p_offset " << p_offset << " should be " << (p_offset % p_align) << " but is " << (p.segments.at(i).start % p_align) << '\n' << end();
      return;
    }

    // prepare for next segment
    p_offset += size;
  }
#undef O
#undef emit
}

void write_segment(const segment& s, ostream& out) {
  for (int i = 0;  i < SIZE(s.lines);  ++i) {
    const vector<word>& w = s.lines.at(i).words;
    for (int j = 0;  j < SIZE(w);  ++j) {
      uint8_t x = hex_byte(w.at(j).data);  // we're done with metadata by this point
      out.write(reinterpret_cast<const char*>(&x), /*sizeof(byte)*/1);
    }
  }
}

uint32_t size_of(const segment& s) {
  uint32_t sum = 0;
  for (int i = 0;  i < SIZE(s.lines);  ++i)
    sum += SIZE(s.lines.at(i).words);
  return sum;
}

:(before "End Includes")
using std::ios;
