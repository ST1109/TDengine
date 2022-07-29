---
title: Naming & Restrictions
---

## Naming Rules

1. Only characters from the English alphabet, digits and underscore are allowed
2. Names cannot start with a digit
3. Case insensitive without escape character "\`"
4. Identifier with escape character "\`"
   To support more flexible table or column names, a new escape character "\`" is introduced. For more details please refer to [escape](/taos-sql/escape).

## Password Rule

The legal character set is `[a-zA-Z0-9!?$%^&*()_–+={[}]:;@~#|<,>.?/]`.

## General Limits

- Maximum length of database name is 32 bytes, and it can't include "." or special characters.
- Maximum length of table name is 192 bytes, excluding the database name prefix and the separator.
- Maximum length of each data row is 48K bytes. Please note that the upper limit includes the extra 2 bytes consumed by each column of BINARY/NCHAR type.
- Maximum length of column name is 64.
- Maximum number of columns is 4096. There must be at least 2 columns, and the first column must be timestamp.
- Maximum length of tag name is 64.
- Maximum number of tags is 128. There must be at least 1 tag. The total length of tag values should not exceed 16K bytes.
- Maximum length of singe SQL statement is 1048576, i.e. 1 MB. It can be configured in the parameter `maxSQLLength` in the client side, the applicable range is [65480, 1048576].
- At most 4096 columns can be returned by `SELECT`. Functions in the query statement constitute columns. An error is returned if the limit is exceeded.
- Maximum numbers of databases, STables, tables are dependent only on the system resources.
- Maximum number of replicas for a database is 3.
- Maximum length of user name is 23 bytes.
- Maximum length of password is 15 bytes.
- Maximum number of rows depends only on the storage space.
- Maximum number of vnodes for a single database is 1024.

## Restrictions of Table/Column Names

### Name Restrictions of Table/Column

The name of a table or column can only be composed of ASCII characters, digits and underscore and it cannot start with a digit. The maximum length is 192 bytes. Names are case insensitive. The name mentioned in this rule doesn't include the database name prefix and the separator.

### Name Restrictions After Escaping

To support more flexible table or column names, new escape character "\`" is introduced in TDengine to avoid the conflict between table name and keywords and break the above restrictions for table names. The escape character is not counted in the length of table name.

With escaping, the string inside escape characters are case sensitive, i.e. will not be converted to lower case internally.

For example:
\`aBc\` and \`abc\` are different table or column names, but "abc" and "aBc" are same names because internally they are all "abc".

:::note
The characters inside escape characters must be printable characters.

:::
