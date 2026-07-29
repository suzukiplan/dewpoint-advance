    .section .rodata
    .balign 4

    .global _binary_crt_vert_spv_start
    .global _binary_crt_vert_spv_end
_binary_crt_vert_spv_start:
    .incbin "src/shaders/crt.vert.spv"
_binary_crt_vert_spv_end:

    .balign 4
    .global _binary_crt_frag_spv_start
    .global _binary_crt_frag_spv_end
_binary_crt_frag_spv_start:
    .incbin "src/shaders/crt.frag.spv"
_binary_crt_frag_spv_end:

    .section .note.GNU-stack,"",@progbits
