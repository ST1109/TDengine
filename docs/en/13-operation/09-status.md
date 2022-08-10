---
sidebar_label: Connections & Tasks
title: Manage Connections and Query Tasks
---

A system operator can use the TDengine CLI to show connections, ongoing queries, stream computing, and can close connections or stop ongoing query tasks or stream computing.

## Show Connections

```sql
SHOW CONNECTIONS;
```

One column of the output of the above SQL command is "ip:port", which is the end point of the client.

## Force Close Connections

```sql
KILL CONNECTION <connection-id>;
```

In the above SQL command, `connection-id` is from the first column of the output of `SHOW CONNECTIONS`.

## Show Ongoing Queries

```sql
SHOW QUERIES;
```

The first column of the output is query ID, which is composed of the corresponding connection ID and the sequence number of the current query task started on this connection. The format is "connection-id:query-no".

## Force Close Queries

```sql
KILL QUERY <query-id>;
```

In the above SQL command, `query-id` is from the first column of the output of `SHOW QUERIES `.

## Show Continuous Query

```sql
SHOW STREAMS;
```

The first column of the output is stream ID, which is composed of the connection ID and the sequence number of the current stream started on this connection. The format is "connection-id:stream-no".

## Force Close Continuous Query

```sql
KILL STREAM <stream-id>;
```

The above SQL command, `stream-id` is from the first column of the output of `SHOW STREAMS`.
