package com.github.jvsena42.mandacaru.domain.model

enum class PaymentDirection { INBOUND, OUTBOUND }

enum class PaymentStatus { PENDING, SUCCEEDED, FAILED }

/**
 * Discriminates the payment protocol. Affects which fields are meaningful
 * and which privacy/fee properties apply.
 */
enum class PaymentType {
    /** Standard BOLT-11 invoice. */
    BOLT11,

    /** BOLT-11 via a Just-In-Time channel opened by an LSP (higher fee possible). */
    BOLT11_JIT,

    /** BOLT-12 offer-based payment (reusable, stateless invoice). */
    BOLT12_OFFER,

    /** BOLT-12 refund flow. */
    BOLT12_REFUND,

    /** Keysend / spontaneous payment (no invoice). */
    SPONTANEOUS,

    /** On-chain payment tracked by the integrated BDK wallet. */
    ONCHAIN,
}

/**
 * Domain model for a Lightning (or on-chain) payment record.
 *
 * [bolt11Invoice] is stored only for inbound payments where we generated
 * the invoice. It is never available for outbound BOLT-11 payments (ldk-node
 * does not persist the invoice after payment).
 *
 * Security note: never display [paymentPreimage] in logs or analytics.
 * The preimage is the proof-of-payment secret; leaking it does not allow
 * theft but can be used to prove payment by a third party.
 */
data class LightningPayment(
    val id: String,
    val amountMsat: ULong?,
    val feesMsat: ULong,
    val direction: PaymentDirection,
    val status: PaymentStatus,
    val type: PaymentType,
    val timestampSecs: ULong,
    /** Human-readable memo. Available for BOLT-11 payments; null otherwise. */
    val description: String?,
    /** Payment hash (hex). Present for BOLT-11, BOLT-12 and spontaneous payments. */
    val paymentHash: String?,
    /**
     * Original BOLT-11 invoice string (stored by ReceivePaymentScreen at
     * generation time — not available from ldk-node PaymentDetails alone).
     */
    val bolt11Invoice: String?,
) {
    val amountSats: ULong? get() = amountMsat?.div(1_000u)
    val feesSats: ULong get() = feesMsat / 1_000u

    /** Signed amount for display: positive = received, negative = sent. */
    val signedAmountMsat: Long?
        get() = amountMsat?.let {
            if (direction == PaymentDirection.INBOUND) it.toLong() else -(it + feesMsat).toLong()
        }
}
