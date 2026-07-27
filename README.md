# mariadb-plugin-date-extra

![mariabd-plugin-date-extra](logo/date_extra.png)

`date_extra` is a MariaDB 13.1 function plugin.
It adds date bucketing, rounding, business-day, and ISO week-date
helpers that are not native MariaDB Server functions.

## Functions

| Function | Description |
|---|---|
| `DATE_BIN(width_seconds, datetime[, origin])` | Floors a date/time into fixed-width buckets. The default origin is `1970-01-01 00:00:00`. |
| `TIME_BUCKET(width_seconds, datetime[, origin])` | Alias with the same fixed-width bucket semantics. |
| `DATE_CEIL(datetime, unit)` | Returns the input when already on a boundary, otherwise the next boundary. |
| `DATE_ROUND(datetime, unit)` | Returns the nearest boundary; exact midpoints round upward. |
| `BUSINESS_DAYS_BETWEEN(start, finish)` | Counts Monday-Friday days in the half-open range `[start, finish)`. Reversing the arguments negates the result. |
| `ADD_BUSINESS_DAYS(date, days)` | Adds or subtracts Monday-Friday days. Weekends are skipped; holidays are not modeled. |
| `ISO_WEEK_DATE(date)` | Formats a date as `YYYY-Www-D` using ISO week-year rules. |

Rounding units are `microsecond`, `second`, `minute`, `hour`, `day`, `week`
(Monday boundary), `month`, `quarter`, and `year`; plural spellings are also
accepted. `DATE_CEIL()` and `DATE_ROUND()` inherit the fractional-second
precision of the `datetime` argument, the same way `DATE_BIN()` and
`TIME_BUCKET()` inherit it from theirs — a value with no fractional seconds
in, none out.

`ISO_WEEK_DATE()` normally returns 10 characters (`YYYY-Www-D`), but for the
last few days of December in year 9999 the ISO week can belong to week 1 of
the following year, so the year component may print as 5 digits.

## Usage examples

### `DATE_BIN()`

Place a value into a fixed 15-minute bucket:

```sql
SELECT DATE_BIN(15 * 60, '2026-07-27 12:34:56');
+------------------------------------------+
| DATE_BIN(15 * 60, '2026-07-27 12:34:56') |
+------------------------------------------+
| 2026-07-27 12:30:00                      |
+------------------------------------------+
1 row in set (0.001 sec)
```

An optional origin controls the alignment of the buckets:

```sql
SELECT DATE_BIN(3600, '2026-07-27 12:34:56',
                '2026-07-27 00:30:00');
+--------------------------------------------------------------+
| DATE_BIN(3600, '2026-07-27 12:34:56', '2026-07-27 00:30:00') |
+--------------------------------------------------------------+
| 2026-07-27 12:30:00                                          |
+--------------------------------------------------------------+
1 row in set (0.000 sec)
```

### `DATE_CEIL()`

Move a value to the next hour boundary. A value already on the requested
boundary is returned unchanged:

```sql
SELECT DATE_CEIL('2026-07-27 12:34:56.123456', 'hour');
+-------------------------------------------------+
| DATE_CEIL('2026-07-27 12:34:56.123456', 'hour') |
+-------------------------------------------------+
| 2026-07-27 13:00:00.000000                      |
+-------------------------------------------------+
1 row in set (0.000 sec)

SELECT DATE_CEIL('2026-07-27 12:00:00', 'hour');
+------------------------------------------+
| DATE_CEIL('2026-07-27 12:00:00', 'hour') |
+------------------------------------------+
| 2026-07-27 12:00:00                      |
+------------------------------------------+
1 row in set (0.001 sec)

SELECT DATE_CEIL('2026-07-27 12:34:56.123456', 'minute');
+---------------------------------------------------+
| DATE_CEIL('2026-07-27 12:34:56.123456', 'minute') |
+---------------------------------------------------+
| 2026-07-27 12:35:00.000000                        |
+---------------------------------------------------+
1 row in set (0.001 sec)
```

### `DATE_ROUND()`

Round to the nearest hour. Exact midpoints round upward:

```sql
SELECT DATE_ROUND('2026-07-27 12:29:59', 'hour');
+-------------------------------------------+
| DATE_ROUND('2026-07-27 12:29:59', 'hour') |
+-------------------------------------------+
| 2026-07-27 12:00:00                       |
+-------------------------------------------+
1 row in set (0.000 sec)

SELECT DATE_ROUND('2026-07-27 12:30:00', 'hour');
+-------------------------------------------+
| DATE_ROUND('2026-07-27 12:30:00', 'hour') |
+-------------------------------------------+
| 2026-07-27 13:00:00                       |
+-------------------------------------------+
1 row in set (0.001 sec)
```

### `TIME_BUCKET()`

Place a value into a one-day bucket. `TIME_BUCKET()` has the same arguments
and behavior as `DATE_BIN()`:

```sql
SELECT TIME_BUCKET(24 * 60 * 60, '2026-07-27 12:34:56');
+--------------------------------------------------+
| TIME_BUCKET(24 * 60 * 60, '2026-07-27 12:34:56') |
+--------------------------------------------------+
| 2026-07-27 00:00:00                              |
+--------------------------------------------------+
1 row in set (0.001 sec)
```

### `BUSINESS_DAYS_BETWEEN()`

Count weekdays in the half-open range `[start, finish)`. In this example,
Friday is counted and the following Monday is the excluded end:

```sql
SELECT BUSINESS_DAYS_BETWEEN('2026-07-24', '2026-07-27');
+---------------------------------------------------+
| BUSINESS_DAYS_BETWEEN('2026-07-24', '2026-07-27') |
+---------------------------------------------------+
|                                                 1 |
+---------------------------------------------------+
1 row in set (0.001 sec)

SELECT BUSINESS_DAYS_BETWEEN('2026-07-27', '2026-07-24');
+---------------------------------------------------+
| BUSINESS_DAYS_BETWEEN('2026-07-27', '2026-07-24') |
+---------------------------------------------------+
|                                                -1 |
+---------------------------------------------------+
1 row in set (0.000 sec)
```

### `ADD_BUSINESS_DAYS()`

Add or subtract weekdays while skipping Saturday and Sunday:

```sql
SELECT ADD_BUSINESS_DAYS('2026-07-24', 1);
+------------------------------------+
| ADD_BUSINESS_DAYS('2026-07-24', 1) |
+------------------------------------+
| 2026-07-27                         |
+------------------------------------+
1 row in set (0.001 sec)

SELECT ADD_BUSINESS_DAYS('2026-07-27', -1);
+-------------------------------------+
| ADD_BUSINESS_DAYS('2026-07-27', -1) |
+-------------------------------------+
| 2026-07-24                          |
+-------------------------------------+
1 row in set (0.001 sec)
```

### `ISO_WEEK_DATE()`

Format a date using its ISO week-year, week number, and weekday. The ISO year
can differ from the calendar year near New Year's Day:

```sql
SELECT ISO_WEEK_DATE('2021-01-01');
+-----------------------------+
| ISO_WEEK_DATE('2021-01-01') |
+-----------------------------+
| 2020-W53-5                  |
+-----------------------------+
1 row in set (0.000 sec)
```

## Comparison with PostgreSQL and MySQL

The table compares this plugin with PostgreSQL 18 and MySQL 8.4. “Composable”
means that the result can be built from other native functions, but there is
no native function with the same purpose and interface.

| Capability | `date_extra` on MariaDB 13.1 | PostgreSQL 18 | MySQL 8.4 |
|---|---|---|---|
| Fixed-width binning | `DATE_BIN(seconds, value[, origin])` and `TIME_BUCKET(...)` | Native `date_bin(interval, source, origin)` | No direct function; composable from timestamp arithmetic |
| Ceiling to a calendar boundary | `DATE_CEIL(value, unit)` | No direct function; `date_trunc()` plus interval arithmetic | No direct function; composable with `DATE_ADD()`, `TIMESTAMPADD()`, and extraction functions |
| Rounding to the nearest boundary | `DATE_ROUND(value, unit)` | No direct function; composable from `date_trunc()` and interval arithmetic | No direct function; composable from date/time arithmetic |
| Business-day difference | `BUSINESS_DAYS_BETWEEN(start, finish)` | No direct core function; typically implemented with `generate_series()` or a calendar table | No direct function; typically implemented with a calendar table or recursive CTE |
| Add business days | `ADD_BUSINESS_DAYS(date, days)` | No direct core function; typically implemented with a calendar table or `generate_series()` | No direct function; typically implemented with a calendar table or recursive CTE |
| ISO week-date text | `ISO_WEEK_DATE(date)` | Composable with `to_char()` ISO fields | Composable with `DATE_FORMAT()`, `YEARWEEK()`, and `WEEKDAY()` |

There are two important compatibility details:

- PostgreSQL's `date_bin` accepts an `interval` stride and requires an explicit
  origin. This plugin accepts a positive integer width in seconds and defaults
  the origin to `1970-01-01 00:00:00`. Consequently, PostgreSQL calls are not
  source-compatible with the plugin.
- `TIME_BUCKET()` is not a PostgreSQL core function; it is commonly associated
  with extensions such as TimescaleDB. In this plugin it is an exact alias for
  `DATE_BIN()` and does not implement extension-specific overloads, time-zone
  handling, or month-sized buckets.

The business-day functions treat Monday through Friday as business days.
Neither weekends nor public holidays are configurable, so applications that
need regional holidays should continue to use a calendar table.

References: [PostgreSQL date/time functions][postgres-datetime] and
[MySQL date/time functions][mysql-datetime].

[postgres-datetime]: https://www.postgresql.org/docs/current/functions-datetime.html
[mysql-datetime]: https://dev.mysql.com/doc/refman/8.4/en/date-and-time-functions.html

## Build and install

Place this directory at `plugin/date_extra` in a MariaDB Server source tree,
configure the server build, and build the `date_extra` target:

```sh
cmake --build /path/to/build --target date_extra
```

Then install and inspect the plugin:

```sql
INSTALL SONAME 'date_extra';
SELECT plugin_name, plugin_type, plugin_library, plugin_description,
       plugin_author FROM information_schema.PLUGINS WHERE plugin_library LIKE 'date_extra.so';
+-----------------------+-------------+----------------+----------------------------------+---------------+
| plugin_name           | plugin_type | plugin_library | plugin_description               | plugin_author |
+-----------------------+-------------+----------------+----------------------------------+---------------+
| date_bin              | FUNCTION    | date_extra.so  | Function DATE_BIN()              | lefred        |
| date_ceil             | FUNCTION    | date_extra.so  | Function DATE_CEIL()             | lefred        |
| date_round            | FUNCTION    | date_extra.so  | Function DATE_ROUND()            | lefred        |
| time_bucket           | FUNCTION    | date_extra.so  | Function TIME_BUCKET()           | lefred        |
| business_days_between | FUNCTION    | date_extra.so  | Function BUSINESS_DAYS_BETWEEN() | lefred        |
| add_business_days     | FUNCTION    | date_extra.so  | Function ADD_BUSINESS_DAYS()     | lefred        |
| iso_week_date         | FUNCTION    | date_extra.so  | Function ISO_WEEK_DATE()         | lefred        |
+-----------------------+-------------+----------------+----------------------------------+---------------+
7 rows in set (0.002 sec)
```

Uninstalling the SONAME removes all seven functions:

```sql
UNINSTALL SONAME 'date_extra';
```
