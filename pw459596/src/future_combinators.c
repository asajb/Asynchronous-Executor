#include "future_combinators.h"
#include <stdlib.h>

#include "future.h"
#include "waker.h"
#include "executor.h"

/* future_then: a combinator to chain two futures sequentially.
 * A full implementation would progress the first future, then pass its result to the second.
 * If the first future fails, the then future should fail as well.
 * If the second future fails, the then future should fail with a different error code.
 */
static FutureState then_progress(Future* base, Mio* mio, Waker waker) {
    ThenFuture* self = (ThenFuture*) base;
    
    // Progress the first future if not yet completed.
    if (!self->fut1_completed) {
        FutureState state1 = self->fut1->progress(self->fut1, mio, waker);
        if (state1 == FUTURE_PENDING) {
            return FUTURE_PENDING;
        }
        if (state1 == FUTURE_FAILURE) {
            base->errcode = THEN_FUTURE_ERR_FUT1_FAILED;
            return FUTURE_FAILURE;
        }
        self->fut1_completed = true;
        // Pass the successful result of fut1 to fut2.
        self->fut2->arg = self->fut1->ok;
    }
    
    // Now progress the second future.
    FutureState state2 = self->fut2->progress(self->fut2, mio, waker);
    if (state2 == FUTURE_PENDING)
        return FUTURE_PENDING;
    if (state2 == FUTURE_FAILURE) {
        base->errcode = THEN_FUTURE_ERR_FUT2_FAILED;
        return FUTURE_FAILURE;
    }
    
    base->ok = self->fut2->ok;
    return FUTURE_COMPLETED;
}

ThenFuture future_then(Future* fut1, Future* fut2) {
    ThenFuture tf;
    tf.fut1 = fut1;
    tf.fut2 = fut2;
    tf.fut1_completed = false;
    tf.base.progress = then_progress;
    tf.base.is_active = false;
    tf.base.arg = NULL;
    tf.base.ok = NULL;
    tf.base.errcode = FUTURE_SUCCESS;
    return tf;
}

/* future_join: a combinator to run two futures concurrently.
 * A proper implementation would invoke both futures' progress() functions 
 * and record their states. When both futures have completed, 
 * the join's progress returns COMPLETED if both succeeded (or FAILURE otherwise).
 */
static FutureState join_progress(Future* base, Mio* mio, Waker waker) {
    JoinFuture* self = (JoinFuture*) base;
    FutureState state1, state2;
    
    // Progress fut1 if not already completed or failed.
    if (self->fut1_completed == FUTURE_PENDING) {
        state1 = self->fut1->progress(self->fut1, mio, waker);
        if (state1 == FUTURE_PENDING) {
            // Do nothing yet.
        } else if (state1 == FUTURE_COMPLETED) {
            self->fut1_completed = FUTURE_COMPLETED;
            self->result.fut1.ok = self->fut1->ok;
            self->result.fut1.errcode = FUTURE_SUCCESS;
        } else if (state1 == FUTURE_FAILURE) {
            self->fut1_completed = FUTURE_FAILURE;
            self->result.fut1.errcode = self->fut1->errcode;
            self->result.fut1.ok = NULL;
        }
    }
    
    // Progress fut2 if not already completed or failed.
    if (self->fut2_completed == FUTURE_PENDING) {
        state2 = self->fut2->progress(self->fut2, mio, waker);
        if (state2 == FUTURE_PENDING) {
            // Do nothing.
        } else if (state2 == FUTURE_COMPLETED) {
            self->fut2_completed = FUTURE_COMPLETED;
            self->result.fut2.ok = self->fut2->ok;
            self->result.fut2.errcode = FUTURE_SUCCESS;
        } else if (state2 == FUTURE_FAILURE) {
            self->fut2_completed = FUTURE_FAILURE;
            self->result.fut2.errcode = self->fut2->errcode;
            self->result.fut2.ok = NULL;
        }
    }
    
    // If either future is still pending, we must return PENDING.
    if (self->fut1_completed == FUTURE_PENDING || self->fut2_completed == FUTURE_PENDING) {
        return FUTURE_PENDING;
    }
    
    // Both futures are done – now check for failures.
    if (self->fut1_completed == FUTURE_FAILURE && self->fut2_completed == FUTURE_FAILURE) {
        base->errcode = JOIN_FUTURE_ERR_BOTH_FUTS_FAILED;
        return FUTURE_FAILURE;
    } else if (self->fut1_completed == FUTURE_FAILURE) {
        base->errcode = JOIN_FUTURE_ERR_FUT1_FAILED;
        return FUTURE_FAILURE;
    } else if (self->fut2_completed == FUTURE_FAILURE) {
        base->errcode = JOIN_FUTURE_ERR_FUT2_FAILED;
        return FUTURE_FAILURE;
    }
    
    // If both succeeded, we could combine the results in base->ok however desired.
    // For example, you might choose fut1->ok, or package both results in a struct.
    base->ok = self->result.fut1.ok; // (or combine both)
    return FUTURE_COMPLETED;
}

JoinFuture future_join(Future* fut1, Future* fut2)
{
    JoinFuture jf;
    jf.fut1 = fut1;
    jf.fut2 = fut2;
    jf.fut1_completed = FUTURE_PENDING;
    jf.fut2_completed = FUTURE_PENDING;
    jf.base.progress = join_progress;
    jf.result.fut1.errcode = FUTURE_SUCCESS;
    jf.result.fut2.errcode = FUTURE_SUCCESS;
    jf.result.fut1.ok = NULL;
    jf.result.fut2.ok = NULL;
    jf.base.errcode = FUTURE_SUCCESS;
    jf.base.ok = NULL;
    return jf;
}

/* future_select: a combinator to run two futures until one completes.
 * A full implementation would progress both and return the result of the first to complete.
 * For brevity, the implementation below is a stub.
 */
static FutureState select_progress(Future* base, Mio* mio, Waker waker) {
    SelectFuture* self = (SelectFuture*)base;

    // If one future has already completed, return COMPLETED immediately.
    if (self->which_completed == SELECT_COMPLETED_FUT1) {
        base->ok = self->fut1->ok;
        return FUTURE_COMPLETED;
    }
    if (self->which_completed == SELECT_COMPLETED_FUT2) {
        base->ok = self->fut2->ok;
        return FUTURE_COMPLETED;
    }
    // If both futures have already failed, return FAILURE.
    if (self->which_completed == SELECT_FAILED_BOTH) {
        // You may choose which errcode to propagate—here we choose fut1's error.
        base->errcode = self->fut1->errcode;
        return FUTURE_FAILURE;
    }

    // Progress fut1 if it hasn't failed yet.
    if (self->which_completed == SELECT_COMPLETED_NONE || self->which_completed == SELECT_FAILED_FUT2) {
        FutureState state1 = self->fut1->progress(self->fut1, mio, waker);
        if (state1 == FUTURE_COMPLETED) {
            self->which_completed = SELECT_COMPLETED_FUT1;
            base->ok = self->fut1->ok;
            return FUTURE_COMPLETED;
        } else if (state1 == FUTURE_FAILURE) {
            if (self->which_completed == SELECT_FAILED_FUT2)
                self->which_completed = SELECT_FAILED_BOTH;
            else
                self->which_completed = SELECT_FAILED_FUT1;
        }
    }

    // Progress fut2 if it hasn't failed yet.
    if (self->which_completed == SELECT_COMPLETED_NONE || self->which_completed == SELECT_FAILED_FUT1) {
        FutureState state2 = self->fut2->progress(self->fut2, mio, waker);
        if (state2 == FUTURE_COMPLETED) {
            self->which_completed = SELECT_COMPLETED_FUT2;
            base->ok = self->fut2->ok;
            return FUTURE_COMPLETED;
        } else if (state2 == FUTURE_FAILURE) {
            if (self->which_completed == SELECT_FAILED_FUT1)
                self->which_completed = SELECT_FAILED_BOTH;
            else
                self->which_completed = SELECT_FAILED_FUT2;
        }
    }

    // If one future has failed but the other is still pending, we keep waiting.
    return FUTURE_PENDING;
}

SelectFuture future_select(Future* fut1, Future* fut2)
{
    // Stub implementation – a complete version would monitor both and decide.
    SelectFuture sf;
    sf.fut1 = fut1;
    sf.fut2 = fut2;
    sf.which_completed = SELECT_COMPLETED_NONE;
    sf.base.progress = select_progress; // Real progress function to be implemented.
    return sf;
}