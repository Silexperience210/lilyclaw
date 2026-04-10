package com.github.jvsena42.mandacaru.presentation.ui.screens.main

import androidx.annotation.DrawableRes
import com.github.jvsena42.mandacaru.R

/**
 * Navigation destinations for the main bottom-tab bar.
 *
 * [LIGHTNING] is added as the second tab — between Transactions and Node —
 * because it's the most frequently used feature on a daily-driver wallet.
 *
 * Tab order (left → right):
 *   Transactions | Lightning | Node | Blockchain | Settings
 */
enum class Destinations(
    val route: String,
    val label: String,
    @DrawableRes val icon: Int,
) {
    TRANSACTION("transaction", "Transactions", R.drawable.ic_transaction),
    LIGHTNING("lightning", "Lightning", R.drawable.ic_bolt),
    NODE("node", "Node", R.drawable.ic_node),
    BLOCKCHAIN("blockchain", "Blockchain", R.drawable.ic_blockchain),
    SETTINGS("settings", "Settings", R.drawable.ic_settings),
    ;

    // ── Lightning sub-routes (not tabs; navigated via NavHost) ────────────────
    companion object {
        const val ROUTE_LIGHTNING_SETUP    = "lightning/setup"
        const val ROUTE_LIGHTNING_SEND     = "lightning/send"
        const val ROUTE_LIGHTNING_RECEIVE  = "lightning/receive"
        const val ROUTE_LIGHTNING_CHANNELS = "lightning/channels"
        const val ROUTE_OPEN_CHANNEL       = "lightning/channels/open"
        const val ROUTE_CHANNEL_DETAIL     = "lightning/channels/{channelId}"
        const val ROUTE_PAYMENT_DETAIL     = "lightning/payments/{paymentId}"

        fun channelDetailRoute(channelId: String) = "lightning/channels/$channelId"
        fun paymentDetailRoute(paymentId: String) = "lightning/payments/$paymentId"
    }
}
