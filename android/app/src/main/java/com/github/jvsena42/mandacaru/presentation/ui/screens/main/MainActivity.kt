package com.github.jvsena42.mandacaru.presentation.ui.screens.main

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import androidx.navigation.NavDestination.Companion.hierarchy
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import com.github.jvsena42.mandacaru.presentation.service.FlorestaService
import com.github.jvsena42.mandacaru.presentation.ui.screens.blockchain.BlockchainScreen
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.LightningScreen
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.channels.ChannelDetailScreen
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.channels.ChannelsScreen
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.channels.OpenChannelScreen
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.payments.PaymentDetailScreen
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.payments.ReceivePaymentScreen
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.payments.SendPaymentScreen
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.setup.LightningSetupScreen
import com.github.jvsena42.mandacaru.presentation.ui.screens.node.NodeScreen
import com.github.jvsena42.mandacaru.presentation.ui.screens.settings.SettingsScreen
import com.github.jvsena42.mandacaru.presentation.ui.screens.transaction.TransactionScreen
import com.github.jvsena42.mandacaru.presentation.ui.theme.MandacaruTheme

class MainActivity : ComponentActivity() {

    private val exitReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action == FlorestaService.ACTION_EXIT_APP) finish()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        ContextCompat.registerReceiver(
            this,
            exitReceiver,
            IntentFilter(FlorestaService.ACTION_EXIT_APP),
            ContextCompat.RECEIVER_NOT_EXPORTED,
        )

        startForegroundService(Intent(this, FlorestaService::class.java))

        setContent {
            MandacaruTheme {
                MandacaruNavGraph()
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        unregisterReceiver(exitReceiver)
    }
}

@Composable
private fun MandacaruNavGraph() {
    val navController = rememberNavController()
    val navBackStackEntry by navController.currentBackStackEntryAsState()
    val currentDestination = navBackStackEntry?.destination

    // Tab destinations — show bottom bar only on top-level screens
    val topLevelRoutes = Destinations.entries.map { it.route }
    val showBottomBar = currentDestination?.route in topLevelRoutes

    Scaffold(
        bottomBar = {
            if (showBottomBar) {
                MandacaruBottomBar(
                    currentRoute = currentDestination?.route,
                    onNavigate = { dest ->
                        navController.navigate(dest.route) {
                            popUpTo(navController.graph.findStartDestination().id) { saveState = true }
                            launchSingleTop = true
                            restoreState = true
                        }
                    },
                )
            }
        },
    ) { innerPadding ->
        NavHost(
            navController = navController,
            startDestination = Destinations.TRANSACTION.route,
            modifier = Modifier.padding(innerPadding),
            enterTransition = { fadeIn(spring(Spring.DampingRatioNoBouncy, Spring.StiffnessMedium)) },
            exitTransition = { fadeOut(spring(Spring.DampingRatioNoBouncy, Spring.StiffnessMedium)) },
        ) {
            // ── Existing tabs ─────────────────────────────────────────────────
            composable(Destinations.TRANSACTION.route) { TransactionScreen() }
            composable(Destinations.NODE.route) { NodeScreen() }
            composable(Destinations.BLOCKCHAIN.route) { BlockchainScreen() }
            composable(Destinations.SETTINGS.route) { SettingsScreen() }

            // ── Lightning tab (hub) ───────────────────────────────────────────
            composable(Destinations.LIGHTNING.route) {
                LightningScreen(
                    onNavigateToSetup = { navController.navigate(Destinations.ROUTE_LIGHTNING_SETUP) },
                    onNavigateToSend = { navController.navigate(Destinations.ROUTE_LIGHTNING_SEND) },
                    onNavigateToReceive = { navController.navigate(Destinations.ROUTE_LIGHTNING_RECEIVE) },
                    onNavigateToChannels = { navController.navigate(Destinations.ROUTE_LIGHTNING_CHANNELS) },
                    onNavigateToChannelDetail = { id -> navController.navigate(Destinations.channelDetailRoute(id)) },
                    onNavigateToPaymentDetail = { id -> navController.navigate(Destinations.paymentDetailRoute(id)) },
                    onNavigateToOpenChannel = { navController.navigate(Destinations.ROUTE_OPEN_CHANNEL) },
                )
            }

            // ── Lightning sub-screens ─────────────────────────────────────────
            composable(Destinations.ROUTE_LIGHTNING_SETUP) {
                LightningSetupScreen(
                    onSetupComplete = {
                        navController.navigate(Destinations.LIGHTNING.route) {
                            popUpTo(Destinations.ROUTE_LIGHTNING_SETUP) { inclusive = true }
                        }
                    },
                    onDismiss = { navController.popBackStack() },
                )
            }
            composable(Destinations.ROUTE_LIGHTNING_SEND) {
                SendPaymentScreen(
                    onBack = { navController.popBackStack() },
                    onPaymentDispatched = { navController.popBackStack() },
                )
            }
            composable(Destinations.ROUTE_LIGHTNING_RECEIVE) {
                ReceivePaymentScreen(onBack = { navController.popBackStack() })
            }
            composable(Destinations.ROUTE_LIGHTNING_CHANNELS) {
                ChannelsScreen(
                    onBack = { navController.popBackStack() },
                    onNavigateToChannelDetail = { id -> navController.navigate(Destinations.channelDetailRoute(id)) },
                    onNavigateToOpenChannel = { navController.navigate(Destinations.ROUTE_OPEN_CHANNEL) },
                )
            }
            composable(Destinations.ROUTE_OPEN_CHANNEL) {
                OpenChannelScreen(
                    onBack = { navController.popBackStack() },
                    onChannelOpened = { navController.popBackStack() },
                )
            }
            composable(
                route = Destinations.ROUTE_CHANNEL_DETAIL,
                arguments = listOf(navArgument("channelId") { type = NavType.StringType }),
            ) { backStackEntry ->
                ChannelDetailScreen(
                    channelId = backStackEntry.arguments?.getString("channelId") ?: "",
                    onBack = { navController.popBackStack() },
                    onChannelClosed = { navController.popBackStack() },
                )
            }
            composable(
                route = Destinations.ROUTE_PAYMENT_DETAIL,
                arguments = listOf(navArgument("paymentId") { type = NavType.StringType }),
            ) { backStackEntry ->
                PaymentDetailScreen(
                    paymentId = backStackEntry.arguments?.getString("paymentId") ?: "",
                    onBack = { navController.popBackStack() },
                )
            }
        }
    }
}

@Composable
private fun MandacaruBottomBar(
    currentRoute: String?,
    onNavigate: (Destinations) -> Unit,
) {
    NavigationBar(
        containerColor = MaterialTheme.colorScheme.surface,
        tonalElevation = 0.dp,
    ) {
        Destinations.entries.forEach { dest ->
            val selected = currentRoute == dest.route
            NavigationBarItem(
                selected = selected,
                onClick = { onNavigate(dest) },
                icon = {
                    if (dest == Destinations.LIGHTNING) {
                        Icon(
                            imageVector = Icons.Default.Bolt,
                            contentDescription = dest.label,
                        )
                    } else {
                        Icon(
                            painter = painterResource(dest.icon),
                            contentDescription = dest.label,
                        )
                    }
                },
                label = {
                    Text(
                        text = dest.label,
                        style = MaterialTheme.typography.labelSmall.copy(
                            fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal,
                            letterSpacing = 0.sp,
                        ),
                    )
                },
                colors = NavigationBarItemDefaults.colors(
                    selectedIconColor = MaterialTheme.colorScheme.primary,
                    selectedTextColor = MaterialTheme.colorScheme.primary,
                    unselectedIconColor = MaterialTheme.colorScheme.onSurfaceVariant,
                    unselectedTextColor = MaterialTheme.colorScheme.onSurfaceVariant,
                    indicatorColor = MaterialTheme.colorScheme.primaryContainer,
                ),
            )
        }
    }
}
