# Lightning Network — Architecture deep-dive

## Stack overview

```
┌──────────────────────────────────────────────────────────────────────┐
│                         Android App (Kotlin)                        │
│                                                                      │
│  Compose UI ← StateFlow ← LightningViewModel ← LightningRepository  │
│                                                        ↑             │
│                                          LightningRepositoryImpl     │
│                                                        ↑             │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │              FlorestaService  (Foreground)                   │    │
│  │                                                             │    │
│  │  ┌──────────────────────┐   ┌──────────────────────────┐   │    │
│  │  │  FlorestaDaemon      │   │  LightningNodeManager    │   │    │
│  │  │  (Rust/UniFFI)       │   │  (ldk-node/UniFFI)       │   │    │
│  │  │                      │   │                          │   │    │
│  │  │  RPC  :8332          │   │  chain ← Electrum        │   │    │
│  │  │  Electrum :50001 ────┼───┼──► localhost:50001       │   │    │
│  │  └──────────────────────┘   │  gossip ← RGS snapshot   │   │    │
│  │                              │  LSPS2 ← LSP peer        │   │    │
│  │                              │  keys  ← Android KS      │   │    │
│  │                              └──────────────────────────┘   │    │
│  └─────────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────────┘
```

## Connectivity strategy

### Problem: Mobile has no stable public IP

A mobile device sits behind NAT and changes IP on every WiFi/4G transition.
Announcing a public address would break channel reachability after every handover.

### Solution: LSP as the stable relay

We configure **LSPS2** (bLIP-52): the LSP holds a well-connected routing node
with a stable IP. Inbound payments route through the LSP, which pushes them to
us over an existing (or newly opened JIT) channel.

- **Outbound payments**: routed via our local gossip graph, no LSP involvement.
- **Inbound payments**: LSP acts as our public-facing endpoint.
- **Channel monitoring**: ldk-node's chain monitor watches via Floresta Electrum.

### IP change handling

When the network callback fires (WiFi→4G), `LightningNodeManager` waits 3 s
for the radio stack to stabilise, then calls `refreshAll()`. ldk-node handles
peer reconnection internally. No manual peer reconnect is needed.

## Security model

### Key storage

```
BIP-39 mnemonic (12 words)
      │
      ▼ AES-256-GCM (Android Keystore)
EncryptedSharedPreferences ("lightning_keystore_v1")
      │
      ▼ read once per node start
ldk-node Builder.setEntropyBip39Mnemonic()
      │
      ▼ never returned to Kotlin layer
LDK channel keys + BDK on-chain wallet
```

- Mnemonic is never logged, never included in crash reports.
- Backup screen sets `FLAG_SECURE` to prevent screenshots.
- Android Keystore requests StrongBox (dedicated security chip) with TEE fallback.

### Channel security

- **Anchor channels** enabled by default: fee-bumping via CPFP is available
  even if mempool fees spike during a force-close.
- **0-conf trust** is restricted to the configured LSP pubkey only.
  Arbitrary peers can never skip the confirmation wait.
- **Justice transactions**: ldk-node persists channel monitors in SQLite.
  Losing the SQLite file while a channel is open means losing the ability
  to punish a cheating counterparty — but the mnemonic still recovers funds
  if the peer is cooperative (or after the breach timeout expires).

### Invoice validation

ldk-node validates BOLT-11 and BOLT-12 signatures internally before processing.
We never modify an invoice string in Kotlin (trimming aside).

## Startup sequence

```
FlorestaService.onCreate()
  │
  ├─ florestaDaemon.start()
  │
  ├─ poll getBlockchainInfo() until RPC responds  ← Electrum is ready at this point
  │
  ├─ read LightningPreferences from DataStore
  │
  ├─ if (prefs.isEnabled && prefs.isSetupComplete)
  │     lightningNodeManager.start(ldkNetwork, prefs)
  │       ├─ keystoreManager.getOrGenerateMnemonic()
  │       ├─ Builder.setChainSourceElectrum("127.0.0.1:50001")
  │       ├─ Builder.setGossipSourceRgs(prefs.rgsUrl)
  │       ├─ Builder.setLiquiditySourceLsps2(...)   ← if LSP configured
  │       └─ node.start()  → spawns ldk-node tokio runtime
  │
  └─ polling loop (10 s) → update notification
```

## Floresta Electrum ports (localhost)

| Network  | RPC port | Electrum port |
|----------|----------|---------------|
| mainnet  | 8332     | **50001**     |
| testnet  | 18332    | **60001**     |
| signet   | 38332    | **60601**     |
| regtest  | 18443    | **60401**     |

## ldk-node version: 0.7.0

Key features used:
- `set_chain_source_electrum()` — Floresta integration
- `set_gossip_source_rgs()` — fast mobile startup
- `set_liquidity_source_lsps2()` — JIT inbound channels
- `build_with_sqlite_store()` — robust persistence
- `bolt11_payment().receive_via_jit_channel()` — LSPS2 receive
- `next_event_async()` — coroutine-native event loop
- Anchor channels — safe force-close fee bumping
