#include "evaluation_mask.h"


 const evaluation_mask_t evaluation_mask_instance = {
         .rank_masks =  {0xff00000000000000,
                         0xff000000000000,
                         0xff0000000000,
                         0xff00000000,
                         0xff000000,
                         0xff0000,
                         0xff00,
                         0xff},
         .file_masks = {0x101010101010101,
                        0x202020202020202,
                        0x404040404040404,
                        0x808080808080808,
                        0x1010101010101010,
                        0x2020202020202020,
                        0x4040404040404040,
                        0x8080808080808080},
         .isolated_file_mask = {0x202020202020202,
                                0x505050505050505,
                                0xa0a0a0a0a0a0a0a,
                                0x1414141414141414,
                                0x2828282828282828,
                                0x5050505050505050,
                                0xa0a0a0a0a0a0a0a0,
                                0x4040404040404040,
                                0x202020202020202,
                                0x505050505050505,
                                0xa0a0a0a0a0a0a0a,
                                0x1414141414141414,
                                0x2828282828282828,
                                0x5050505050505050,
                                0xa0a0a0a0a0a0a0a0,
                                0x4040404040404040,
                                0x202020202020202,
                                0x505050505050505,
                                0xa0a0a0a0a0a0a0a,
                                0x1414141414141414,
                                0x2828282828282828,
                                0x5050505050505050,
                                0xa0a0a0a0a0a0a0a0,
                                0x4040404040404040,
                                0x202020202020202,
                                0x505050505050505,
                                0xa0a0a0a0a0a0a0a,
                                0x1414141414141414,
                                0x2828282828282828,
                                0x5050505050505050,
                                0xa0a0a0a0a0a0a0a0,
                                0x4040404040404040,
                                0x202020202020202,
                                0x505050505050505,
                                0xa0a0a0a0a0a0a0a,
                                0x1414141414141414,
                                0x2828282828282828,
                                0x5050505050505050,
                                0xa0a0a0a0a0a0a0a0,
                                0x4040404040404040,
                                0x202020202020202,
                                0x505050505050505,
                                0xa0a0a0a0a0a0a0a,
                                0x1414141414141414,
                                0x2828282828282828,
                                0x5050505050505050,
                                0xa0a0a0a0a0a0a0a0,
                                0x4040404040404040,
                                0x202020202020202,
                                0x505050505050505,
                                0xa0a0a0a0a0a0a0a,
                                0x1414141414141414,
                                0x2828282828282828,
                                0x5050505050505050,
                                0xa0a0a0a0a0a0a0a0,
                                0x4040404040404040,
                                0x202020202020202,
                                0x505050505050505,
                                0xa0a0a0a0a0a0a0a,
                                0x1414141414141414,
                                0x2828282828282828,
                                0x5050505050505050,
                                0xa0a0a0a0a0a0a0a0,
                                0x4040404040404040
         },
         .passed_pawn_mask = {
                 /* white passed pawn mask by square. */
                 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                 0x3, 0x7, 0xe, 0x1c, 0x38, 0x70, 0xe0, 0xc0,
                 0x303, 0x707, 0xe0e, 0x1c1c, 0x3838, 0x7070, 0xe0e0, 0xc0c0,
                 0x30303, 0x70707, 0xe0e0e, 0x1c1c1c, 0x383838, 0x707070, 0xe0e0e0, 0xc0c0c0,
                 0x3030303, 0x7070707, 0xe0e0e0e, 0x1c1c1c1c, 0x38383838, 0x70707070, 0xe0e0e0e0, 0xc0c0c0c0,
                 0x303030303, 0x707070707, 0xe0e0e0e0e, 0x1c1c1c1c1c, 0x3838383838, 0x7070707070, 0xe0e0e0e0e0, 0xc0c0c0c0c0,
                 0x30303030303, 0x70707070707, 0xe0e0e0e0e0e, 0x1c1c1c1c1c1c, 0x383838383838, 0x707070707070, 0xe0e0e0e0e0e0, 0xc0c0c0c0c0c0,
                 0x3030303030303, 0x7070707070707, 0xe0e0e0e0e0e0e, 0x1c1c1c1c1c1c1c, 0x38383838383838, 0x70707070707070, 0xe0e0e0e0e0e0e0, 0xc0c0c0c0c0c0c0,

                 /* black passed pawn mask by square. */
                 0x303030303030300, 0x707070707070700, 0xe0e0e0e0e0e0e00, 0x1c1c1c1c1c1c1c00, 0x3838383838383800, 0x7070707070707000, 0xe0e0e0e0e0e0e000, 0xc0c0c0c0c0c0c000,
                 0x303030303030000, 0x707070707070000, 0xe0e0e0e0e0e0000, 0x1c1c1c1c1c1c0000, 0x3838383838380000, 0x7070707070700000, 0xe0e0e0e0e0e00000, 0xc0c0c0c0c0c00000,
                 0x303030303000000, 0x707070707000000, 0xe0e0e0e0e000000, 0x1c1c1c1c1c000000, 0x3838383838000000, 0x7070707070000000, 0xe0e0e0e0e0000000, 0xc0c0c0c0c0000000,
                 0x303030300000000, 0x707070700000000, 0xe0e0e0e00000000, 0x1c1c1c1c00000000, 0x3838383800000000, 0x7070707000000000, 0xe0e0e0e000000000, 0xc0c0c0c000000000,
                 0x303030000000000, 0x707070000000000, 0xe0e0e0000000000, 0x1c1c1c0000000000, 0x3838380000000000, 0x7070700000000000, 0xe0e0e00000000000, 0xc0c0c00000000000,
                 0x303000000000000, 0x707000000000000, 0xe0e000000000000, 0x1c1c000000000000, 0x3838000000000000, 0x7070000000000000, 0xe0e0000000000000, 0xc0c0000000000000,
                 0x300000000000000, 0x700000000000000, 0xe00000000000000, 0x1c00000000000000, 0x3800000000000000, 0x7000000000000000, 0xe000000000000000, 0xc000000000000000,
                 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
         }
 };
