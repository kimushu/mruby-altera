#include "mruby.h"
#include "mruby/class.h"
#include "mruby/data.h"
#include "mruby/range.h"

struct avalon_pio_regs
{
  uint32_t data;
  uint32_t direction;
  uint32_t interruptmask;
  uint32_t edgecapture;

  /*
   * Registers below is available when
   * `Enable individual bit set/clear output register' is turned on
   */
  uint32_t outset;
  uint32_t outclear;
  uint32_t _dummy[2];
};

struct pio_data
{
  struct pio_data *owner;
  uint16_t refs;
  uint8_t msb;
  uint8_t lsb;
  struct avalon_pio_regs *reg;
  uint32_t mask;
  uint32_t polarity; /* Owner only */
};

static void
pio_free(mrb_state *mrb, void *ptr)
{
  struct pio_data *data;
  data = (struct pio_data *)ptr;

  if (data->refs == 0) {
    /* Not an owner */
    pio_free(mrb, data->owner);
    mrb_free(mrb, data);
  }
  else if (data->refs == 1) {
    /* Disappearing owner */
    mrb_free(mrb, data);
  }
  else {
    --data->refs;
  }
}

static const struct mrb_data_type pio_type = {"PIOCore", pio_free};

static mrb_value
pio_wrap(mrb_state *mrb, struct RClass *cls, void *data)
{
  return mrb_obj_value(Data_Wrap_Struct(mrb, cls, &pio_type, data));
}

static mrb_value
pio_initialize(mrb_state *mrb, mrb_value self)
{
  mrb_int base, width;
  struct pio_data *data;

  mrb_get_args(mrb, "ii", &base, &width);

  if ((base & (sizeof(struct avalon_pio_regs) - 1)) != 0) {
    mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid base 0x%x", base);
  }

  if (width > 32 || width < 1) {
    mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid width %d", width);
  }

  data = (struct pio_data *)mrb_calloc(mrb, 1, sizeof(struct pio_data));
  data->owner = data;
  data->refs = 1;
  data->msb = width - 1;
  data->lsb = 0;
  data->reg = (struct avalon_pio_regs *)base;
  data->mask = (1u << width) - 1;
  data->polarity = 0; /* All pins are initialized as active-high */

  return pio_wrap(mrb, mrb_class_ptr(self), data);
}

static mrb_value
pio_width(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  return mrb_fixnum_value(data->msb - data->lsb + 1);
}

static mrb_value
pio_priv_msb(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  return mrb_fixnum_value(data->msb);
}

static mrb_value
pio_priv_lsb(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  return mrb_fixnum_value(data->lsb);
}

static mrb_value
pio_priv_base(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  return mrb_fixnum_value((mrb_int)data->reg);
}

static mrb_value
pio_slice(mrb_state *mrb, mrb_value self)
{
  struct pio_data *src_data, *new_data;
  mrb_int msb, lsb;
  mrb_value arg;

  src_data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);

  mrb_get_args(mrb, "o", &arg);

  msb = lsb = -1;
  if (mrb_fixnum_p(arg)) {
    /* single bit */
    msb = lsb = mrb_fixnum(arg);
  }
  else if (mrb_type(arg) == MRB_TT_RANGE) {
    /* multiple bits */
    struct RRange *range = mrb_range_ptr(arg);
    if (mrb_fixnum_p(range->edges->beg)) {
      msb = mrb_fixnum(range->edges->beg);
    }
    if (!range->excl &&
        mrb_fixnum_p(range->edges->end)) {
      lsb = mrb_fixnum(range->edges->end);
    }
  }

  if (src_data->refs == 0) {
    msb += src_data->lsb;
    lsb += src_data->lsb;
  }

  if (msb > src_data->msb || lsb < src_data->lsb || msb < lsb) {
    mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid range");
  }

  new_data = (struct pio_data *)mrb_calloc(mrb, 1, sizeof(struct pio_data));
  new_data->owner = src_data->owner;
  new_data->refs = 0;
  new_data->msb = msb;
  new_data->lsb = lsb;
  new_data->reg = src_data->reg;
  new_data->mask = ((1u << (msb - lsb + 1)) - 1) << lsb;
  ++new_data->owner->refs;

  return pio_wrap(mrb, mrb_class_ptr(self), new_data);
}

static mrb_value
pio_high(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  __builtin_stwio(&data->reg->outset, data->mask);
  return self;
}

static mrb_value
pio_is_high(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  if (data->msb > data->lsb) {
    mrb_raisef(mrb, E_TYPE_ERROR, "invalid use of high?");
  }
  if ((__builtin_ldwio(&data->reg->data) & data->mask) == data->mask) {
    return mrb_true_value();
  }
  else {
    return mrb_false_value();
  }
}

static mrb_value
pio_low(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  __builtin_stwio(&data->reg->outclear, data->mask);
  return self;
}

static mrb_value
pio_is_low(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  if (data->msb > data->lsb) {
    mrb_raisef(mrb, E_TYPE_ERROR, "invalid use of low?");
  }
  if ((__builtin_ldwio(&data->reg->data) & data->mask) == 0) {
    return mrb_true_value();
  }
  else {
    return mrb_false_value();
  }
}

static mrb_value
pio_toggle(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  uint32_t cur;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  cur = __builtin_ldwio(&data->reg->data) & data->mask;
  __builtin_stwio(&data->reg->outset, cur ^ data->mask);
  __builtin_stwio(&data->reg->outclear, cur);
  return self;
}

static mrb_value
pio_assert(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  __builtin_stwio(&data->reg->outset, data->mask & (~data->owner->polarity));
  __builtin_stwio(&data->reg->outclear, data->mask & data->owner->polarity);
  return self;
}

static mrb_value
pio_is_asserted(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  if (data->msb > data->lsb) {
    mrb_raisef(mrb, E_TYPE_ERROR, "invalid use of asseted?");
  }
  if (((__builtin_ldwio(&data->reg->data) ^ data->owner->polarity) & data->mask)
      == data->mask) {
    return mrb_true_value();
  }
  else {
    return mrb_false_value();
  }
}

static mrb_value
pio_negate(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  __builtin_stwio(&data->reg->outset, data->mask & data->owner->polarity);
  __builtin_stwio(&data->reg->outclear, data->mask & (~data->owner->polarity));
  return self;
}

static mrb_value
pio_is_negated(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  if (data->msb > data->lsb) {
    mrb_raisef(mrb, E_TYPE_ERROR, "invalid use of negated?");
  }
  if (((__builtin_ldwio(&data->reg->data) ^ data->owner->polarity) & data->mask)
      == 0) {
    return mrb_true_value();
  }
  else {
    return mrb_false_value();
  }
}

static mrb_value
pio_enable_output(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  uint32_t dir;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  dir = __builtin_ldwio(&data->reg->direction);
  __builtin_stwio(&data->reg->direction, dir | data->mask);
  return self;
}

static mrb_value
pio_disable_output(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  uint32_t dir;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  dir = __builtin_ldwio(&data->reg->direction);
  __builtin_stwio(&data->reg->direction, dir & ~data->mask);
  return self;
}

static mrb_value
pio_is_output_enabled(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  if (data->msb > data->lsb) {
    mrb_raisef(mrb, E_TYPE_ERROR, "invalid use of output_enabled?");
  }
  if ((__builtin_ldwio(&data->reg->direction) & data->mask) == data->mask) {
    return mrb_true_value();
  }
  else {
    return mrb_false_value();
  }
}

static mrb_value
pio_is_output_disabled(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  if (data->msb > data->lsb) {
    mrb_raisef(mrb, E_TYPE_ERROR, "invalid use of output_disabled?");
  }
  if ((__builtin_ldwio(&data->reg->direction) & data->mask) == 0) {
    return mrb_true_value();
  }
  else {
    return mrb_false_value();
  }
}

static mrb_value
pio_active_high(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  data->owner->polarity &= ~data->mask;
  return self;
}

static mrb_value
pio_is_active_high(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  if (data->msb > data->lsb) {
    mrb_raisef(mrb, E_TYPE_ERROR, "invalid use of active_high?");
  }
  if ((data->owner->polarity & data->mask) == 0) {
    return mrb_true_value();
  }
  else {
    return mrb_false_value();
  }
}

static mrb_value
pio_is_active_low(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  if (data->msb > data->lsb) {
    mrb_raisef(mrb, E_TYPE_ERROR, "invalid use of active_low?");
  }
  if ((data->owner->polarity & data->mask) == data->mask) {
    return mrb_true_value();
  }
  else {
    return mrb_false_value();
  }
}

static mrb_value
pio_active_low(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  data->owner->polarity |= data->mask;
  return self;
}

static mrb_value
pio_value(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  mrb_int width;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  width = (data->msb - data->lsb + 1);
#if defined(MRB_WORD_BOXING)
  if (width > (MRB_INT_BIT - MRB_FIXNUM_SHIFT)) {
#else
  if (width > MRB_INT_BIT) {
#endif
    mrb_raisef(mrb, E_TYPE_ERROR, "width is too large to treat as mrb_int");
  }
  return mrb_fixnum_value(
    (mrb_int)(__builtin_ldwio(&data->reg->data) & data->mask) / (1 << data->lsb)
  );
}

static mrb_value
pio_value_set(mrb_state *mrb, mrb_value self)
{
  struct pio_data *data;
  mrb_int width;
  uint32_t value;
  data = DATA_GET_PTR(mrb, self, &pio_type, struct pio_data);
  mrb_get_args(mrb, "i", &value);
  width = (data->msb - data->lsb + 1);
#if defined(MRB_WORD_BOXING)
  if (width > (MRB_INT_BIT - MRB_FIXNUM_SHIFT)) {
#else
  if (width > MRB_INT_BIT) {
#endif
    mrb_raisef(mrb, E_TYPE_ERROR, "width is too large to treat as mrb_int");
  }

  value <<= data->lsb;
  value &= data->mask;
  __builtin_stwio(&data->reg->outset, value);
  __builtin_stwio(&data->reg->outclear, value ^ data->mask);

  return self;
}

void
altera_piocore_init(mrb_state *mrb, struct RClass *mod)
{
  struct RClass *cls;

  cls = mrb_define_class_under(mrb, mod, "PIOCore", mrb->object_class);
  MRB_SET_INSTANCE_TT(cls, MRB_TT_DATA);
  mrb_define_method(mrb, cls, "initialize"      , pio_initialize        , MRB_ARGS_REQ(2));
  mrb_define_method(mrb, cls, "width"           , pio_width             , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "__msb__"         , pio_priv_msb          , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "__lsb__"         , pio_priv_lsb          , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "__base__"        , pio_priv_base         , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "slice"           , pio_slice             , MRB_ARGS_REQ(1));
  mrb_define_alias (mrb, cls, "[]"              , "slice");
  mrb_define_method(mrb, cls, "high"            , pio_high              , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "high?"           , pio_is_high           , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "low"             , pio_low               , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "low?"            , pio_is_low            , MRB_ARGS_NONE());
  mrb_define_alias (mrb, cls, "set"             , "high");
  mrb_define_alias (mrb, cls, "set?"            , "high?");
  mrb_define_alias (mrb, cls, "clear"           , "low");
  mrb_define_alias (mrb, cls, "cleared?"        , "low?");
  mrb_define_method(mrb, cls, "toggle"          , pio_toggle            , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "assert"          , pio_assert            , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "negate"          , pio_negate            , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "asserted?"       , pio_is_asserted       , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "negated?"        , pio_is_negated        , MRB_ARGS_NONE());
  mrb_define_alias (mrb, cls, "on"              , "assert");
  mrb_define_alias (mrb, cls, "on?"             , "asserted?");
  mrb_define_alias (mrb, cls, "off"             , "negate");
  mrb_define_alias (mrb, cls, "off?"            , "negated?");
  mrb_define_method(mrb, cls, "enable_output"   , pio_enable_output     , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "output_enabled?" , pio_is_output_enabled , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "disable_output"  , pio_disable_output    , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "output_disabled?", pio_is_output_disabled, MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "active_high"     , pio_active_high       , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "active_high?"    , pio_is_active_high    , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "active_low"      , pio_active_low        , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "active_low?"     , pio_is_active_low     , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "value"           , pio_value             , MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "value="          , pio_value_set         , MRB_ARGS_REQ(1));
}

void
altera_piocore_final(mrb_state *mrb)
{
}
