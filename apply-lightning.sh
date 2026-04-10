#!/bin/bash
# ─────────────────────────────────────────────────────────────────────────────
# apply-lightning.sh
#
# Applique la feature Lightning Network dans ton fork Mandacaru,
# puis commit et push automatiquement.
#
# UNE SEULE COMMANDE à lancer depuis la racine de ton fork Mandacaru :
#
#   bash <(curl -s https://raw.githubusercontent.com/Silexperience210/lilyclaw/mandacaru-lightning/apply-lightning.sh)
#
# ─────────────────────────────────────────────────────────────────────────────
set -e

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'

info()    { echo -e "${GREEN}[✓]${NC} $1"; }
warn()    { echo -e "${YELLOW}[!]${NC} $1"; }
error()   { echo -e "${RED}[✗]${NC} $1"; exit 1; }
step()    { echo -e "\n${CYAN}▶ $1${NC}"; }

# ── 1. Vérification structure Mandacaru ───────────────────────────────────────
step "Vérification du projet..."
[[ -f "app/build.gradle.kts" ]]                                        || error "Lance ce script depuis la racine de ton fork Mandacaru."
[[ -d "app/src/main/java/com/github/jvsena42/mandacaru" ]]             || error "Structure Android introuvable."
command -v git &>/dev/null                                              || error "git n'est pas installé."
git rev-parse --is-inside-work-tree &>/dev/null                        || error "Ce dossier n'est pas un dépôt git."
info "Projet Mandacaru détecté."

BRANCH=$(git rev-parse --abbrev-ref HEAD)
info "Branche active : $BRANCH"

# ── 2. Téléchargement des sources Lightning ───────────────────────────────────
step "Téléchargement des fichiers Lightning depuis lilyclaw..."
TMPDIR_WORK=$(mktemp -d)
trap "rm -rf $TMPDIR_WORK" EXIT

git clone --depth=1 --branch=mandacaru-lightning \
    https://github.com/Silexperience210/lilyclaw.git \
    "$TMPDIR_WORK/src" --quiet
info "Sources téléchargées."

SRC="$TMPDIR_WORK/src/android/app/src/main/java/com/github/jvsena42/mandacaru"
DEST="app/src/main/java/com/github/jvsena42/mandacaru"

# ── 3. Copie des fichiers ─────────────────────────────────────────────────────
step "Copie des fichiers..."

info "data/lightning (KeystoreManager, LightningNodeManager, Repository)..."
mkdir -p "$DEST/data/lightning"
cp -r "$SRC/data/lightning/"* "$DEST/data/lightning/"

info "data/preferences (LightningPreference*)..."
mkdir -p "$DEST/data/preferences"
cp "$SRC/data/preferences/LightningPreferenceKeys.kt" "$DEST/data/preferences/"
cp "$SRC/data/preferences/LightningPreferences.kt"    "$DEST/data/preferences/"

info "domain/model (Lightning*)..."
cp "$SRC/domain/model/LightningBalance.kt"   "$DEST/domain/model/"
cp "$SRC/domain/model/LightningChannel.kt"   "$DEST/domain/model/"
cp "$SRC/domain/model/LightningNodeEvent.kt" "$DEST/domain/model/"
cp "$SRC/domain/model/LightningNodeState.kt" "$DEST/domain/model/"
cp "$SRC/domain/model/LightningPayment.kt"   "$DEST/domain/model/"

info "presentation/service (FlorestaService)..."
cp "$SRC/presentation/service/FlorestaService.kt" "$DEST/presentation/service/"

info "presentation/ui/components (BalanceCard, CapacityBar, PaymentItem)..."
mkdir -p "$DEST/presentation/ui/components"
cp "$SRC/presentation/ui/components/LightningBalanceCard.kt" "$DEST/presentation/ui/components/"
cp "$SRC/presentation/ui/components/ChannelCapacityBar.kt"   "$DEST/presentation/ui/components/"
cp "$SRC/presentation/ui/components/PaymentListItem.kt"      "$DEST/presentation/ui/components/"

info "presentation/ui/screens/lightning/..."
mkdir -p "$DEST/presentation/ui/screens/lightning/channels"
mkdir -p "$DEST/presentation/ui/screens/lightning/payments"
mkdir -p "$DEST/presentation/ui/screens/lightning/setup"
cp "$SRC/presentation/ui/screens/lightning/LightningScreen.kt"    "$DEST/presentation/ui/screens/lightning/"
cp "$SRC/presentation/ui/screens/lightning/LightningViewModel.kt" "$DEST/presentation/ui/screens/lightning/"
cp "$SRC/presentation/ui/screens/lightning/channels/"*.kt  "$DEST/presentation/ui/screens/lightning/channels/"
cp "$SRC/presentation/ui/screens/lightning/payments/"*.kt  "$DEST/presentation/ui/screens/lightning/payments/"
cp "$SRC/presentation/ui/screens/lightning/setup/"*.kt     "$DEST/presentation/ui/screens/lightning/setup/"

info "MandacaruApplication.kt (Koin modules mis à jour)..."
cp "$SRC/MandacaruApplication.kt" "$DEST/"

info "Navigation (Destinations, MainActivity)..."
cp "$SRC/presentation/ui/screens/main/Destinations.kt" "$DEST/presentation/ui/screens/main/"
cp "$SRC/presentation/ui/screens/main/MainActivity.kt"  "$DEST/presentation/ui/screens/main/"

# ── 4. Dépendances build.gradle.kts ──────────────────────────────────────────
step "Mise à jour de app/build.gradle.kts..."
GRADLE="app/build.gradle.kts"

if grep -q "ldk-node-android" "$GRADLE"; then
    warn "ldk-node-android déjà présent — build.gradle.kts non modifié."
else
    # Insère après la ligne JNA (déjà présente dans Mandacaru)
    sed -i '/implementation(libs.jna)/a\
\
    // ── Lightning Network ────────────────────────────────────────────────\
    implementation("org.lightningdevkit:ldk-node-android:0.7.0")\
    implementation("androidx.security:security-crypto:1.1.0-alpha06")\
    implementation("com.google.zxing:core:3.5.3")' "$GRADLE"
    info "3 dépendances ajoutées (ldk-node, security-crypto, zxing)."
fi

# ── 5. ndk abiFilters (arm64-v8a) ────────────────────────────────────────────
if ! grep -q "abiFilters" "$GRADLE"; then
    warn "Pense à ajouter 'abiFilters += listOf(\"arm64-v8a\")' dans le bloc ndk {} de $GRADLE si tu cibles arm64 uniquement."
fi

# ── 6. Git : add + commit + push ──────────────────────────────────────────────
step "Commit et push automatique..."

# Stage tout ce qu'on a créé/modifié
git add \
    "$DEST/data/lightning/" \
    "$DEST/data/preferences/LightningPreferenceKeys.kt" \
    "$DEST/data/preferences/LightningPreferences.kt" \
    "$DEST/domain/model/LightningBalance.kt" \
    "$DEST/domain/model/LightningChannel.kt" \
    "$DEST/domain/model/LightningNodeEvent.kt" \
    "$DEST/domain/model/LightningNodeState.kt" \
    "$DEST/domain/model/LightningPayment.kt" \
    "$DEST/presentation/service/FlorestaService.kt" \
    "$DEST/presentation/ui/components/LightningBalanceCard.kt" \
    "$DEST/presentation/ui/components/ChannelCapacityBar.kt" \
    "$DEST/presentation/ui/components/PaymentListItem.kt" \
    "$DEST/presentation/ui/screens/lightning/" \
    "$DEST/MandacaruApplication.kt" \
    "$DEST/presentation/ui/screens/main/Destinations.kt" \
    "$DEST/presentation/ui/screens/main/MainActivity.kt" \
    "$GRADLE"

git commit -m "feat: add Lightning Network support via ldk-node

- ldk-node v0.7.0 with Floresta Electrum as chain source (localhost)
- LSPS2 JIT inbound channels (mobile-friendly, no public IP needed)
- Anchor channels with CPFP fee-bumping on force-close
- RGS rapid gossip sync (startup in seconds, not minutes)
- BIP-39 mnemonic encrypted in Android Keystore (StrongBox/TEE)
- Send/Receive BOLT-11, reusable BOLT-12 offers
- Full channel management (open, cooperative close, force close)
- Apple-standard Compose UI with spring animations
- FlorestaService updated: starts LN after Floresta RPC is ready"

# Push vers origin (même branche que celle en cours)
git push -u origin "$BRANCH"
info "Pushé sur origin/$BRANCH ✓"

# ── 7. Résumé final ───────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${GREEN}  Lightning Network appliqué, commité et pushé ! ⚡           ${NC}"
echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "  Branche : $BRANCH"
echo ""
echo "  Prochaine étape — builder l'APK de debug :"
echo ""
echo -e "    ${CYAN}./gradlew assembleDebug${NC}"
echo ""
echo "  Si le build échoue avec une erreur 'currentNetwork',"
echo "  ouvre un ticket ici et je corrige en 2 min."
echo ""
