
#ifndef __BUFFER_CACHE_WRITEBACK_IMPL_HPP__
#define __BUFFER_CACHE_WRITEBACK_IMPL_HPP__

template <class config_t>
writeback_tmpl_t<config_t>::writeback_tmpl_t(cache_t *cache,
        bool delay_commits, unsigned int flush_interval_ms)
    : delay_commits(delay_commits),
      interval_ms(flush_interval_ms),
      cache(cache),
      state(state_none),
      transaction(NULL) {
}

template <class config_t>
writeback_tmpl_t<config_t>::~writeback_tmpl_t() {
    delete flush_lock;
}

template <class config_t>
void writeback_tmpl_t<config_t>::start() {
    flush_lock =
        new rwi_lock<config_t>(&get_cpu_context()->event_queue->message_hub,
                               get_cpu_context()->event_queue->queue_id);
    timespec ts;
    ts.tv_sec = interval_ms / 1000;
    ts.tv_nsec = (interval_ms % 1000) * 1000 * 1000;
    get_cpu_context()->event_queue->timer_add(&ts, timer_callback, this);
}

template <class config_t>
bool writeback_tmpl_t<config_t>::begin_transaction(transaction_t *txn) {
    assert(txn->get_access() == rwi_read || txn->get_access() == rwi_write);
    if (txn->get_access() == rwi_read)
        return true;
    bool locked = flush_lock->lock(rwi_read, txn);
    return locked;
}

template <class config_t>
bool writeback_tmpl_t<config_t>::commit(transaction_t *txn,
        transaction_commit_callback_t *callback) {
    if (txn->get_access() == rwi_read)
        return true;
    flush_lock->unlock();
    if (!delay_commits)
        return true;
    txns.insert(txn_state_t(txn, callback));
    return false;
}

template <class config_t>
void writeback_tmpl_t<config_t>::aio_complete(buf_t *buf, bool written) {
    if (written)
        writeback(buf);
}

template <class config_t>
void writeback_tmpl_t<config_t>::local_buf_t::set_dirty(buf_t *super) {
    if (!dirty)
        super->pin();
    dirty = true;
    writeback->dirty_blocks.insert(super->get_block_id());
}

template <class config_t>
void writeback_tmpl_t<config_t>::timer_callback(void *ctx) {
    // TODO(NNW): We can't start writeback when it's already started, but we
    // may want a more thorough way of dealing with this case.
    if (static_cast<writeback_tmpl_t *>(ctx)->state == state_none)
        static_cast<writeback_tmpl_t *>(ctx)->writeback(NULL);
}

template <class config_t>
void writeback_tmpl_t<config_t>::on_lock_available() {
    assert(state == state_locking);
    if (state == state_locking) {
        state = state_locked;
        writeback(NULL);
    }
}

template <class config_t>
void writeback_tmpl_t<config_t>::writeback(buf_t *buf) {
    //printf("Writeback being called, state %d\n", state);

    if (state == state_none) {
        assert(buf == NULL);

        /* Start a read transaction so we can request bufs. */
        assert(transaction == NULL);
        transaction = cache->begin_transaction(rwi_read, NULL);
        assert(transaction != NULL); // Read txns always start immediately.

        /* Request exclusive flush_lock, forcing all write txns to complete. */
        state = state_locking;
        bool locked = flush_lock->lock(rwi_write, this);
        if (locked)
            state = state_locked;
    }
    if (state == state_locked) {
        assert(buf == NULL);
        assert(flush_bufs.empty());
        assert(flush_txns.empty());

        flush_txns = txns;
        txns.clear();

        /* Request read locks on all of the blocks we need to flush. */
        for (typename std::set<block_id_t>::iterator it = dirty_blocks.begin();
            it != dirty_blocks.end(); ++it) {
            buf_t *buf = transaction->acquire(*it, rwi_read, NULL);
            assert(buf); // Acquire must succeed since we hold the flush_lock.
            flush_bufs.insert(buf);
        }

        dirty_blocks.clear();
        flush_lock->unlock(); // Write transactions can now proceed again.

        /* Start writing all the dirty bufs down, as a transaction. */
        typename serializer_t::write *writes =
            (typename serializer_t::write *)calloc(flush_bufs.size(),
                                                   sizeof *writes);
        int i = 0;
        for (typename std::set<buf_t *>::iterator it = flush_bufs.begin();
             it != flush_bufs.end(); ++it, i++) {
            writes[i].block_id = (*it)->get_block_id();
            writes[i].buf = (*it)->ptr();
            writes[i].state = (*it);
        }
        if (flush_bufs.size())
            cache->do_write(get_cpu_context()->event_queue, writes,
                flush_bufs.size());
        free(writes);
        state = state_write_bufs;
    }
    if (state == state_write_bufs) {
        if (buf) {
            assert(flush_bufs.find(buf) != flush_bufs.end());
            flush_bufs.erase(buf);
            buf->set_clean();
            buf->release();
        }
        if (flush_bufs.empty()) {
            /* Notify all waiting transactions of completion. */
            for (typename std::set<txn_state_t>::iterator it =
                flush_txns.begin(); it != flush_txns.end(); ++it) {
                it->first->state = transaction_t::committed;
                // TODO(NNW): We should push notifications through event queue
                it->second->on_txn_commit(it->first);
                delete it->first; // TODO(NNW): There should be a better place.
            }
            flush_txns.clear();

            /* Reset all of our state. */
            bool committed = transaction->commit(NULL);
            assert(committed); // Read-only transactions commit immediately.
            transaction = NULL;
            state = state_none;
            //printf("Writeback complete\n");
        } else {
            //printf("Flush bufs, waiting for %ld more\n", flush_bufs.size());
        }
    }
}

#endif  // __BUFFER_CACHE_WRITEBACK_IMPL_HPP__
