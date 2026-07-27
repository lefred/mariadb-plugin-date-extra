# AI Changes

Changes made by AI assistance in this repository.

## 2026-07-27 — Claude Sonnet 5 (claude-sonnet-5)

Follow-up fixes from a code review of `date_extra.cc`. All three review
findings were addressed:

1. **`next_boundary()` used `current_thd` instead of the `thd` already in
   scope.** `Item_func_date_rounding::get_date(THD *thd, ...)` (used by
   `DATE_CEIL()`/`DATE_ROUND()`) called the file-local helper
   `next_boundary()`, which internally called `date_add_interval(current_thd, ...)`
   instead of using the `thd` parameter passed into `get_date()`. This was
   harmless in practice (the two are the same thread during expression
   evaluation) but inconsistent with the rest of the file, which threads
   `thd` explicitly everywhere else. `next_boundary()` now takes a `THD *thd`
   argument and both call sites pass it through.

2. **`DATE_CEIL()`/`DATE_ROUND()` always emitted 6-digit fractional-second
   precision, even for whole-second inputs**, unlike `DATE_BIN()`/
   `TIME_BUCKET()`, which adapt their output scale to the input's precision.
   `Item_func_date_rounding::fix_length_and_dec()` now calls
   `args[0]->datetime_precision(thd)` instead of hardcoding
   `TIME_SECOND_PART_DIGITS`, so the two function families behave
   consistently. Updated `mysql-test/date_extra/basic.result` and the
   README examples to match the corrected (now fraction-free) output for
   whole-second inputs.

3. **`ISO_WEEK_DATE()` could format a string longer than its declared
   `max_length`.** `fix_length_and_dec()` declared a fixed 10-character
   result (`YYYY-Www-D`), but `calc_week()` can report an ISO year one past
   the input date's calendar year for dates in the last days of December
   (e.g. `'9999-12-31'` can belong to ISO week 1 of year `10000`), producing
   an 11-character string. This mismatch between the declared and actual
   length could cause truncation in contexts that size buffers/columns from
   `max_length` (temp tables, `UNION`, etc.). Widened the declared length to
   11 characters and the output buffer to 24 bytes, with a comment
   explaining why, and documented the 5-digit-year edge case in the README.

No behavioral changes were made beyond these three fixes; the rest of the
plugin's logic was verified correct against the real MariaDB server headers
and the shipped `mysql-test/date_extra/basic.result` expectations.
