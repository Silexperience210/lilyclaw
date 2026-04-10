package com.github.jvsena42.mandacaru

import android.app.Application
import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.preferencesDataStore
import com.github.jvsena42.mandacaru.data.floresta.FlorestaRpc
import com.github.jvsena42.mandacaru.data.floresta.FlorestaRpcImpl
import com.github.jvsena42.mandacaru.data.lightning.KeystoreManager
import com.github.jvsena42.mandacaru.data.lightning.LightningNodeManager
import com.github.jvsena42.mandacaru.data.lightning.LightningRepository
import com.github.jvsena42.mandacaru.data.lightning.LightningRepositoryImpl
import com.github.jvsena42.mandacaru.data.preferences.LightningPreferencesDataSource
import com.github.jvsena42.mandacaru.data.preferences.PreferencesDataSource
import com.github.jvsena42.mandacaru.data.preferences.PreferencesDataSourceImpl
import com.github.jvsena42.mandacaru.domain.floresta.FlorestaDaemon
import com.github.jvsena42.mandacaru.domain.floresta.FlorestaDaemonImpl
import com.github.jvsena42.mandacaru.presentation.ui.screens.blockchain.BlockchainViewModel
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.LightningViewModel
import com.github.jvsena42.mandacaru.presentation.ui.screens.node.NodeViewModel
import com.github.jvsena42.mandacaru.presentation.ui.screens.settings.SettingsViewModel
import com.github.jvsena42.mandacaru.presentation.ui.screens.transaction.TransactionViewModel
import org.koin.android.ext.koin.androidContext
import org.koin.androidx.viewmodel.dsl.viewModel
import org.koin.core.context.startKoin
import org.koin.dsl.module

// Single DataStore instance for the entire app (Floresta + Lightning prefs share it).
val Context.appDataStore: DataStore<Preferences> by preferencesDataStore("mandacaru_preferences")

class MandacaruApplication : Application() {

    override fun onCreate() {
        super.onCreate()
        startKoin {
            androidContext(this@MandacaruApplication)
            modules(dataModule, domainModule, presentationModule)
        }
    }
}

// ── Data layer ────────────────────────────────────────────────────────────────

private val dataModule = module {

    // ── DataStore ─────────────────────────────────────────────────────────────
    single<DataStore<Preferences>> { androidContext().appDataStore }

    // ── Floresta ──────────────────────────────────────────────────────────────
    single<FlorestaDaemon> { FlorestaDaemonImpl(androidContext()) }
    single<FlorestaRpc> { FlorestaRpcImpl(get()) }
    single<PreferencesDataSource> { PreferencesDataSourceImpl(get()) }

    // ── Lightning ─────────────────────────────────────────────────────────────

    // KeystoreManager is a singleton: only one instance accesses the
    // EncryptedSharedPreferences file to avoid concurrent write conflicts.
    single { KeystoreManager(androidContext()) }

    // LightningNodeManager is the single owner of the ldk-node Node object.
    // It must be a singleton so that FlorestaService and all ViewModels
    // observe the same state flows.
    single { LightningNodeManager(androidContext(), get()) }

    single<LightningPreferencesDataSource> { LightningPreferencesDataSource(get()) }

    // Repository delegates to the manager; declared as the interface so
    // ViewModels don't import the manager directly.
    single<LightningRepository> { LightningRepositoryImpl(get()) }
}

// ── Domain layer ──────────────────────────────────────────────────────────────

private val domainModule = module {
    // No separate use-cases in this version; the repository interface covers
    // the required operations without additional indirection.
}

// ── Presentation layer ────────────────────────────────────────────────────────

private val presentationModule = module {

    // Existing Floresta ViewModels
    viewModel { NodeViewModel(get()) }
    viewModel { SettingsViewModel(get(), get()) }
    viewModel { TransactionViewModel(get()) }
    viewModel { BlockchainViewModel(get()) }

    // New Lightning ViewModel
    viewModel { LightningViewModel(get(), get()) }
}
