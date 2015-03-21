#include "mruby.h"

extern void altera_piocore_init(mrb_state *mrb, struct RClass *mod);
extern void altera_piocore_final(mrb_state *mrb);

void
mrb_mruby_altera_gem_init(mrb_state *mrb)
{
  struct RClass *mod;
  mod = mrb_define_module(mrb, "Altera");

  altera_piocore_init(mrb, mod);
}

void
mrb_mruby_altera_gem_final(mrb_state *mrb)
{
  altera_piocore_final(mrb);
}
