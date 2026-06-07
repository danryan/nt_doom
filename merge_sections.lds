/* Partial-link script that collapses gcc's split .text._ZNxxx /
 * .data._ZNxxx etc into single sections. C++ COMDAT for inline class
 * methods emits one .text.<mangled> section per method, which can produce
 * thousands of sections per plug-in; merging into ~6 sections keeps the
 * firmware section walker within budget.
 */
SECTIONS
{
  .text : { *(.text .text.* .gnu.linkonce.t.*) }
  .rodata : { *(.rodata .rodata.* .gnu.linkonce.r.*) }
  .data : { *(.data .data.* .gnu.linkonce.d.*) }
  .data.rel.ro : { *(.data.rel.ro .data.rel.ro.* .gnu.linkonce.d.rel.ro.*) }
  .data.rel.local : { *(.data.rel.local .data.rel.local.* .gnu.linkonce.d.rel.local.*) }
  .bss : { *(.bss .bss.* .gnu.linkonce.b.*) }
  .init_array : { *(.init_array .init_array.*) }
  .fini_array : { *(.fini_array .fini_array.*) }
}
