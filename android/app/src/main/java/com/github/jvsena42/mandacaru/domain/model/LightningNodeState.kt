package com.github.jvsena42.mandacaru.domain.model

/**
 * Represents the full lifecycle state of the Lightning node.
 *
 * State transitions:
 *   Idle → WaitingForBitcoinNode → Starting → Running
 *   Running → Stopping → Idle
 *   Any → Error
 *   Error → Starting (retry)
 *
 * WaitingForBitcoinNode is needed because ldk-node requires the Floresta
 * Electrum server to be reachable before it can start. We wait for the
 * first successful Floresta RPC response before attempting LN startup.
 */
sealed class LightningNodeState {
    /** LN has never been started or was cleanly stopped. */
    object Idle : LightningNodeState()

    /**
     * LN is waiting for the Bitcoin node (Floresta) to expose its
     * Electrum server before it can connect as a chain source.
     */
    object WaitingForBitcoinNode : LightningNodeState()

    /** ldk-node is initializing: loading keys, connecting to Electrum, syncing gossip. */
    object Starting : LightningNodeState()

    /**
     * ldk-node is fully running.
     *
     * @param nodeId  Hex-encoded 33-byte compressed public key of this node.
     */
    data class Running(val nodeId: String) : LightningNodeState()

    /** Clean shutdown in progress. Channels are being closed gracefully. */
    object Stopping : LightningNodeState()

    /**
     * An unrecoverable error occurred. UI should surface [message] and offer
     * a retry or a reset (wipe node data) action.
     *
     * @param message  Human-readable description.
     * @param cause    Original exception for crash-reporting purposes.
     */
    data class Error(val message: String, val cause: Throwable? = null) : LightningNodeState()

    val isOperational: Boolean get() = this is Running
    val isTransitioning: Boolean get() = this is Starting || this is WaitingForBitcoinNode || this is Stopping
    val isIdle: Boolean get() = this is Idle || this is Error
}
