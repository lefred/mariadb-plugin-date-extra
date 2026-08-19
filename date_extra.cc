/* Copyright (c) 2019,2024,2025,2026 MariaDB Corporation
   Copyright (c) 2026 lefred (Frédéric Descamps)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335  USA */

#define MYSQL_SERVER

#include <my_global.h>
#include <sql_class.h>
#include <sql_time.h>
#include <item_timefunc.h>
#include <mysql/plugin_function.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <string>

namespace {

static const longlong USECS_PER_SECOND= 1000000LL;
static const longlong SECONDS_PER_DAY= 86400LL;
static const longlong USECS_PER_DAY= SECONDS_PER_DAY * USECS_PER_SECOND;

template <typename Item_type, uint Minimum, uint Maximum= Minimum>
class Create_date_extra_function : public Create_native_func
{
public:
  Item *create_native(THD *thd, const LEX_CSTRING *name,
                      List<Item> *items) override
  {
    const uint count= items ? items->elements : 0;
    if (count < Minimum || count > Maximum)
    {
      my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name->str);
      return nullptr;
    }
    return new (thd->mem_root) Item_type(thd, *items);
  }
};

static bool read_datetime(THD *thd, Item *item, MYSQL_TIME *value)
{
  return item->get_date(thd, value,
                        date_mode_t(static_cast<ulonglong>(
                          TIME_NO_ZERO_DATE | TIME_NO_ZERO_IN_DATE))) ||
         item->null_value ||
         (value->time_type != MYSQL_TIMESTAMP_DATE &&
          value->time_type != MYSQL_TIMESTAMP_DATETIME);
}

static longlong floor_div(longlong value, longlong divisor)
{
  longlong quotient= value / divisor;
  if (value % divisor < 0)
    --quotient;
  return quotient;
}

static longlong to_linear_usecs(const MYSQL_TIME &value)
{
  const longlong days= calc_daynr(value.year, value.month, value.day);
  const longlong seconds= value.hour * 3600LL + value.minute * 60LL +
                          value.second;
  return days * USECS_PER_DAY + seconds * USECS_PER_SECOND +
         value.second_part;
}

static bool from_linear_usecs(longlong value, MYSQL_TIME *result)
{
  const longlong daynr= floor_div(value, USECS_PER_DAY);
  longlong within_day= value - daynr * USECS_PER_DAY;
  if (daynr <= 0 || daynr > std::numeric_limits<long>::max())
    return true;

  uint year, month, day;
  if (get_date_from_daynr(static_cast<long>(daynr), &year, &month, &day) ||
      year == 0 || year > 9999)
    return true;

  set_zero_time(result, MYSQL_TIMESTAMP_DATETIME);
  result->year= year;
  result->month= month;
  result->day= day;
  result->hour= static_cast<uint>(within_day / (3600LL * USECS_PER_SECOND));
  within_day%= 3600LL * USECS_PER_SECOND;
  result->minute= static_cast<uint>(within_day / (60LL * USECS_PER_SECOND));
  within_day%= 60LL * USECS_PER_SECOND;
  result->second= static_cast<uint>(within_day / USECS_PER_SECOND);
  result->second_part= static_cast<ulong>(within_day % USECS_PER_SECOND);
  return false;
}

class Item_func_fixed_bucket : public Item_datetimefunc
{
protected:
  bool calculate(THD *thd, MYSQL_TIME *result)
  {
    const longlong width_seconds= args[0]->val_int();
    if (args[0]->null_value || width_seconds <= 0 ||
        width_seconds > std::numeric_limits<longlong>::max() /
                            USECS_PER_SECOND)
      return true;

    MYSQL_TIME source;
    if (read_datetime(thd, args[1], &source))
      return true;

    MYSQL_TIME origin;
    if (arg_count == 3)
    {
      if (read_datetime(thd, args[2], &origin))
        return true;
    }
    else
    {
      set_zero_time(&origin, MYSQL_TIMESTAMP_DATETIME);
      origin.year= 1970;
      origin.month= origin.day= 1;
    }

    const longlong width= width_seconds * USECS_PER_SECOND;
    const longlong source_value= to_linear_usecs(source);
    const longlong origin_value= to_linear_usecs(origin);
    const longlong delta= source_value - origin_value;
    const longlong bucket= floor_div(delta, width);
    return from_linear_usecs(origin_value + bucket * width, result);
  }

public:
  using Item_datetimefunc::Item_datetimefunc;
  Item_func_fixed_bucket(THD *thd, List<Item> &items):
    Item_datetimefunc(thd)
  {
    set_arguments(thd, items);
  }

  bool fix_length_and_dec(THD *thd) override
  {
    fix_attributes_datetime(args[1]->datetime_precision(thd));
    set_maybe_null();
    return false;
  }

  bool get_date(THD *thd, MYSQL_TIME *result, date_mode_t) override
  {
    null_value= calculate(thd, result);
    return null_value;
  }
};

#define DECLARE_BUCKET_CLASS(ClassName, SqlName)                           \
class ClassName : public Item_func_fixed_bucket                           \
{                                                                         \
  using Self= ClassName;                                                   \
public:                                                                   \
  using Item_func_fixed_bucket::Item_func_fixed_bucket;                   \
  LEX_CSTRING func_name_cstring() const override                          \
  { static LEX_CSTRING name= SqlName##_LEX_CSTRING; return name; }        \
  Item *shallow_copy(THD *thd) const override                             \
  { return get_item_copy<Self>(thd, this); }                              \
  static Plugin_function *plugin_descriptor()                             \
  {                                                                       \
    static Create_date_extra_function<Self, 2, 3> creator;                \
    static Plugin_function descriptor(&creator);                          \
    return &descriptor;                                                    \
  }                                                                       \
}

DECLARE_BUCKET_CLASS(Item_func_date_bin, "date_bin");
DECLARE_BUCKET_CLASS(Item_func_time_bucket, "time_bucket");

enum Round_unit
{
  UNIT_INVALID,
  UNIT_MICROSECOND,
  UNIT_SECOND,
  UNIT_MINUTE,
  UNIT_HOUR,
  UNIT_DAY,
  UNIT_WEEK,
  UNIT_MONTH,
  UNIT_QUARTER,
  UNIT_YEAR
};

static Round_unit read_unit(Item *item)
{
  StringBuffer<32> buffer;
  String *value= item->val_str(&buffer);
  if (!value || item->null_value)
    return UNIT_INVALID;
  std::string unit(value->ptr(), value->length());
  std::transform(unit.begin(), unit.end(), unit.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (!unit.empty() && unit.back() == 's')
    unit.pop_back();
  if (unit == "microsecond") return UNIT_MICROSECOND;
  if (unit == "second") return UNIT_SECOND;
  if (unit == "minute") return UNIT_MINUTE;
  if (unit == "hour") return UNIT_HOUR;
  if (unit == "day") return UNIT_DAY;
  if (unit == "week") return UNIT_WEEK;
  if (unit == "month") return UNIT_MONTH;
  if (unit == "quarter") return UNIT_QUARTER;
  if (unit == "year") return UNIT_YEAR;
  return UNIT_INVALID;
}

static bool at_boundary(const MYSQL_TIME &value, Round_unit unit)
{
  if (unit == UNIT_MICROSECOND) return true;
  if (value.second_part) return false;
  if (unit == UNIT_SECOND) return true;
  if (value.second) return false;
  if (unit == UNIT_MINUTE) return true;
  if (value.minute) return false;
  if (unit == UNIT_HOUR) return true;
  if (value.hour) return false;
  if (unit == UNIT_DAY) return true;
  if (unit == UNIT_WEEK)
    return calc_weekday(calc_daynr(value.year, value.month, value.day), false) == 0;
  if (value.day != 1) return false;
  if (unit == UNIT_MONTH) return true;
  if (unit == UNIT_QUARTER) return (value.month - 1) % 3 == 0;
  return value.month == 1;
}

static bool truncate_datetime(const MYSQL_TIME &value, Round_unit unit,
                              MYSQL_TIME *result)
{
  *result= value;
  result->time_type= MYSQL_TIMESTAMP_DATETIME;
  if (unit == UNIT_MICROSECOND) return false;
  result->second_part= 0;
  if (unit == UNIT_SECOND) return false;
  result->second= 0;
  if (unit == UNIT_MINUTE) return false;
  result->minute= 0;
  if (unit == UNIT_HOUR) return false;
  result->hour= 0;
  if (unit == UNIT_DAY) return false;
  if (unit == UNIT_WEEK)
  {
    long daynr= calc_daynr(value.year, value.month, value.day);
    daynr-= calc_weekday(daynr, false);
    return get_date_from_daynr(daynr, &result->year, &result->month,
                              &result->day);
  }
  result->day= 1;
  if (unit == UNIT_MONTH) return false;
  if (unit == UNIT_QUARTER)
  {
    result->month= ((value.month - 1) / 3) * 3 + 1;
    return false;
  }
  result->month= 1;
  return false;
}

static bool next_boundary(THD *thd, MYSQL_TIME *value, Round_unit unit)
{
  INTERVAL interval;
  memset(&interval, 0, sizeof(interval));
  interval.day= 1;
  interval_type type= INTERVAL_DAY;
  switch (unit)
  {
  case UNIT_MICROSECOND: interval.day= 0; interval.second_part= 1;
                         type= INTERVAL_MICROSECOND; break;
  case UNIT_SECOND: interval.day= 0; interval.second= 1;
                    type= INTERVAL_SECOND; break;
  case UNIT_MINUTE: interval.day= 0; interval.minute= 1;
                    type= INTERVAL_MINUTE; break;
  case UNIT_HOUR: interval.day= 0; interval.hour= 1;
                  type= INTERVAL_HOUR; break;
  case UNIT_WEEK: interval.day= 7; break;
  case UNIT_MONTH: interval.day= 0; interval.month= 1;
                   type= INTERVAL_MONTH; break;
  case UNIT_QUARTER: interval.day= 0; interval.month= 3;
                     type= INTERVAL_MONTH; break;
  case UNIT_YEAR: interval.day= 0; interval.year= 1;
                  type= INTERVAL_YEAR; break;
  default: break;
  }
  return date_add_interval(thd, value, type, interval);
}

class Item_func_date_rounding : public Item_datetimefunc
{
protected:
  virtual bool rounds_to_nearest() const= 0;

public:
  using Item_datetimefunc::Item_datetimefunc;
  Item_func_date_rounding(THD *thd, List<Item> &items):
    Item_datetimefunc(thd)
  {
    set_arguments(thd, items);
  }

  bool fix_length_and_dec(THD *thd) override
  {
    fix_attributes_datetime(args[0]->datetime_precision(thd));
    set_maybe_null();
    return false;
  }

  bool get_date(THD *thd, MYSQL_TIME *result, date_mode_t) override
  {
    MYSQL_TIME value, lower, upper;
    const Round_unit unit= read_unit(args[1]);
    if (unit == UNIT_INVALID || read_datetime(thd, args[0], &value) ||
        truncate_datetime(value, unit, &lower))
      return null_value= true;

    if (!rounds_to_nearest())
    {
      if (!at_boundary(value, unit) && next_boundary(thd, &lower, unit))
        return null_value= true;
      *result= lower;
      null_value= false;
      return false;
    }

    upper= lower;
    if (next_boundary(thd, &upper, unit))
      return null_value= true;
    const longlong value_us= to_linear_usecs(value);
    const longlong lower_us= to_linear_usecs(lower);
    const longlong upper_us= to_linear_usecs(upper);
    *result= value_us - lower_us < upper_us - value_us ? lower : upper;
    null_value= false;
    return false;
  }
};

#define DECLARE_ROUND_CLASS(ClassName, SqlName, Nearest)                  \
class ClassName : public Item_func_date_rounding                          \
{                                                                         \
  using Self= ClassName;                                                   \
protected:                                                                \
  bool rounds_to_nearest() const override { return Nearest; }             \
public:                                                                   \
  using Item_func_date_rounding::Item_func_date_rounding;                 \
  LEX_CSTRING func_name_cstring() const override                          \
  { static LEX_CSTRING name= SqlName##_LEX_CSTRING; return name; }        \
  Item *shallow_copy(THD *thd) const override                             \
  { return get_item_copy<Self>(thd, this); }                              \
  static Plugin_function *plugin_descriptor()                             \
  {                                                                       \
    static Create_date_extra_function<Self, 2> creator;                   \
    static Plugin_function descriptor(&creator);                          \
    return &descriptor;                                                    \
  }                                                                       \
}

DECLARE_ROUND_CLASS(Item_func_date_ceil, "date_ceil", false);
DECLARE_ROUND_CLASS(Item_func_date_round, "date_round", true);

class Item_func_business_days_between : public Item_longlong_func
{
  using Self= Item_func_business_days_between;
public:
  using Item_longlong_func::Item_longlong_func;
  bool fix_length_and_dec(THD *thd) override
  {
    set_maybe_null();
    return Item_longlong_func::fix_length_and_dec(thd);
  }
  longlong val_int() override
  {
    MYSQL_TIME start, finish;
    if (read_datetime(current_thd, args[0], &start) ||
        read_datetime(current_thd, args[1], &finish))
      return null_value= true, 0;
    longlong first= calc_daynr(start.year, start.month, start.day);
    longlong last= calc_daynr(finish.year, finish.month, finish.day);
    const int sign= first <= last ? 1 : -1;
    if (sign < 0) std::swap(first, last);
    const longlong days= last - first;
    const longlong weeks= days / 7;
    longlong business= weeks * 5;
    for (longlong day= first + weeks * 7; day < last; ++day)
      if (calc_weekday(static_cast<long>(day), false) < 5)
        ++business;
    null_value= false;
    return sign * business;
  }
  LEX_CSTRING func_name_cstring() const override
  { static LEX_CSTRING name= "business_days_between"_LEX_CSTRING; return name; }
  Item *shallow_copy(THD *thd) const override
  { return get_item_copy<Self>(thd, this); }
  static Plugin_function *plugin_descriptor()
  {
    static Create_date_extra_function<Self, 2> creator;
    static Plugin_function descriptor(&creator);
    return &descriptor;
  }
};

class Item_func_add_business_days : public Item_datefunc
{
  using Self= Item_func_add_business_days;
public:
  using Item_datefunc::Item_datefunc;
  Item_func_add_business_days(THD *thd, List<Item> &items):
    Item_datefunc(thd)
  {
    set_arguments(thd, items);
  }
  bool get_date(THD *thd, MYSQL_TIME *result, date_mode_t) override
  {
    MYSQL_TIME value;
    if (read_datetime(thd, args[0], &value))
      return null_value= true;
    longlong remaining= args[1]->val_int();
    if (args[1]->null_value)
      return null_value= true;
    /*
      The supported DATE range contains fewer than four million days.
      Reject larger magnitudes before negation/arithmetic (also covers
      LLONG_MIN) instead of allowing an intermediate overflow.
    */
    if (remaining < -4000000 || remaining > 4000000)
      return null_value= true;
    longlong daynr= calc_daynr(value.year, value.month, value.day);
    const int direction= remaining < 0 ? -1 : 1;
    if (remaining < 0) remaining= -remaining;
    daynr+= direction * (remaining / 5) * 7;
    remaining%= 5;
    while (remaining)
    {
      daynr+= direction;
      if (calc_weekday(static_cast<long>(daynr), false) < 5)
        --remaining;
    }
    set_zero_time(result, MYSQL_TIMESTAMP_DATE);
    null_value= get_date_from_daynr(static_cast<long>(daynr), &result->year,
                                    &result->month, &result->day);
    return null_value;
  }
  LEX_CSTRING func_name_cstring() const override
  { static LEX_CSTRING name= "add_business_days"_LEX_CSTRING; return name; }
  Item *shallow_copy(THD *thd) const override
  { return get_item_copy<Self>(thd, this); }
  static Plugin_function *plugin_descriptor()
  {
    static Create_date_extra_function<Self, 2> creator;
    static Plugin_function descriptor(&creator);
    return &descriptor;
  }
};

class Item_func_iso_week_date : public Item_str_func
{
  using Self= Item_func_iso_week_date;
public:
  using Item_str_func::Item_str_func;
  bool fix_length_and_dec(THD *) override
  {
    /*
      Normally "YYYY-Www-D" (10 chars), but calc_week() can report an
      ISO year one past the input's year (e.g. late December dates
      belonging to week 1 of the next year), so a DATE of '9999-12-3x'
      can format with a 5-digit year. Reserve room for that so the
      declared max_length never falls short of the actual result.
    */
    fix_char_length(11);
    set_maybe_null();
    collation.set(system_charset_info, DERIVATION_COERCIBLE);
    return false;
  }
  String *val_str(String *output) override
  {
    MYSQL_TIME value;
    if (read_datetime(current_thd, args[0], &value))
      return null_value= true, nullptr;
    uint iso_year;
    const uint week= calc_week(&value, WEEK_MONDAY_FIRST | WEEK_YEAR, &iso_year);
    const uint weekday= calc_weekday(
        calc_daynr(value.year, value.month, value.day), false) + 1;
    char text[24];
    const int length= my_snprintf(text, sizeof(text), "%04u-W%02u-%u",
                                  iso_year, week, weekday);
    output->copy(text, static_cast<size_t>(length), system_charset_info);
    null_value= false;
    return output;
  }
  LEX_CSTRING func_name_cstring() const override
  { static LEX_CSTRING name= "iso_week_date"_LEX_CSTRING; return name; }
  Item *shallow_copy(THD *thd) const override
  { return get_item_copy<Self>(thd, this); }
  static Plugin_function *plugin_descriptor()
  {
    static Create_date_extra_function<Self, 1> creator;
    static Plugin_function descriptor(&creator);
    return &descriptor;
  }
};

} // namespace

#define DATE_EXTRA_PLUGIN(ItemClass, PluginName, Description)             \
{                                                                         \
  MariaDB_FUNCTION_PLUGIN, ItemClass::plugin_descriptor(), PluginName,     \
  "lefred", Description, PLUGIN_LICENSE_GPL, nullptr, nullptr, 0x0100,    \
  nullptr, nullptr, "0.2.0", MariaDB_PLUGIN_MATURITY_BETA           \
}

maria_declare_plugin(date_extra)
DATE_EXTRA_PLUGIN(Item_func_date_bin, "date_bin", "Function DATE_BIN()"),
DATE_EXTRA_PLUGIN(Item_func_date_ceil, "date_ceil", "Function DATE_CEIL()"),
DATE_EXTRA_PLUGIN(Item_func_date_round, "date_round", "Function DATE_ROUND()"),
DATE_EXTRA_PLUGIN(Item_func_time_bucket, "time_bucket", "Function TIME_BUCKET()"),
DATE_EXTRA_PLUGIN(Item_func_business_days_between, "business_days_between",
                  "Function BUSINESS_DAYS_BETWEEN()"),
DATE_EXTRA_PLUGIN(Item_func_add_business_days, "add_business_days",
                  "Function ADD_BUSINESS_DAYS()"),
DATE_EXTRA_PLUGIN(Item_func_iso_week_date, "iso_week_date",
                  "Function ISO_WEEK_DATE()")
maria_declare_plugin_end;
