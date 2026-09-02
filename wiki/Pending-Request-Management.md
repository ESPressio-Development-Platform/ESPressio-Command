# Pending Request Management

Outstanding asynchronous requests are tracked by `CommandPendingRequestPool<Capacity>`.

The pool is fixed-capacity and uses an array rather than an unbounded dynamic collection.

## Stored state

Each active entry records:

- request ID;
- destination address;
- absolute response deadline;
- response mode;
- response count.

## Add

Adding can return `Success`, `DuplicateRequestId`, or `CapacityExhausted`. A transport/router must handle capacity exhaustion explicitly rather than growing memory without bound.

## Completion

For `Single` response mode, the first completion removes the entry. For `Multiple`, responses can increment the response count until the final response releases the entry.

## Expiry

`Expire(now, callback)` collects expired entries while holding the pool lock, clears them, then invokes callbacks after releasing the lock. Extension code should preserve this pattern so externally supplied callbacks never execute under the internal pool mutex.

## Timeout registry memory

Path-specific timeout configuration uses ESPressio System's `ExternalPreferred` memory policy for its dynamic registry entries, while the active pending-request pool itself remains statically bounded.