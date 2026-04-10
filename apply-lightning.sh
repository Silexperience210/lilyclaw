#!/bin/bash
# ─────────────────────────────────────────────────────────────────────────────
# apply-lightning.sh
#
# Applique la feature Lightning Network dans ton fork Mandacaru.
# Lance ce script depuis la RACINE de ton fork Mandacaru :
#
#   bash <(curl -s https://raw.githubusercontent.com/Silexperience210/lilyclaw/mandacaru-lightning/apply-lightning.sh)
#
# ─────────────────────────────────────────────────────────────────────────────
set -e

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

info()    { echo -e "${GREEN}[✓]${NC} $1"; }
warn()    { echo -e "${YELLOW}[!]${NC} $1"; }
error()   { echo -e "${RED}[✗]${NC} $1"; exit 1; }

# ── Vérification : on est bien à la racine d'un projet Mandacaru ──────────────
[[ -f "app/build.gradle.kts" ]] || error "Lance ce script depuis la racine de ton fork Mandacaru."
[[ -d "app/src/main/java/com/github/jvsena42/mandacaru" ]] || error "Structure Android introuvable."
info "Projet Mandacaru détecté."

# ── Clone du branch source (lilyclaw) ─────────────────────────────────────────
TMPDIR_WORK=$(mktemp -d)
trap "rm -rf $TMPDIR_WORK" EXIT

info "Téléchargement des fichiers Lightning..."
git clone --depth=1 --branch=mandacaru-lightning \
    https://github.com/Silexperience210/lilyclaw.git \
    "$TMPDIR_WORK/src" --quiet

SRC="$TMPDIR_WORK/src/android/app/src/main/java/com/github/jvsena42/mandacaru"
DEST="app/src/main/java/com/github/jvsena42/mandacaru"

# ── Copie des fichiers ────────────────────────────────────────────────────────
info "Copie data/lightning..."
mkdir -p "$DEST/data/lightning"
cp -r "$SRC/data/lightning/"* "$DEST/data/lightning/"

info "Copie data/preferences (LightningPreference*)..."
mkdir -p "$DEST/data/preferences"
cp "$SRC/data/preferences/LightningPreferenceKeys.kt" "$DEST/data/preferences/"
cp "$SRC/data/preferences/LightningPreferences.kt"    "$DEST/data/preferences/"

info "Copie domain/model (Lightning*)..."
cp "$SRC/domain/model/LightningBalance.kt"   "$DEST/domain/model/"
cp "$SRC/domain/model/LightningChannel.kt"   "$DEST/domain/model/"
cp "$SRC/domain/model/LightningNodeEvent.kt" "$DEST/domain/model/"
cp "$SRC/domain/model/LightningNodeState.kt" "$DEST/domain/model/"
cp "$SRC/domain/model/LightningPayment.kt"   "$DEST/domain/model/"

info "Copie presentation/service (FlorestaService)..."
cp "$SRC/presentation/service/FlorestaService.kt" "$DEST/presentation/service/"

info "Copie presentation/ui/components (Lightning*)..."
mkdir -p "$DEST/presentation/ui/components"
cp "$SRC/presentation/ui/components/LightningBalanceCard.kt" "$DEST/presentation/ui/components/"
cp "$SRC/presentation/ui/components/ChannelCapacityBar.kt"   "$DEST/presentation/ui/components/"
cp "$SRC/presentation/ui/components/PaymentListItem.kt"      "$DEST/presentation/ui/components/"

info "Copie presentation/ui/screens/lightning/..."
mkdir -p "$DEST/presentation/ui/screens/lightning/channels"
mkdir -p "$DEST/presentation/ui/screens/lightning/payments"
mkdir -p "$DEST/presentation/ui/screens/lightning/setup"
cp "$SRC/presentation/ui/screens/lightning/LightningScreen.kt"  "$DEST/presentation/ui/screens/lightning/"
cp "$SRC/presentation/ui/screens/lightning/LightningViewModel.kt" "$DEST/presentation/ui/screens/lightning/"
cp "$SRC/presentation/ui/screens/lightning/channels/"*.kt "$DEST/presentation/ui/screens/lightning/channels/"
cp "$SRC/presentation/ui/screens/lightning/payments/"*.kt "$DEST/presentation/ui/screens/lightning/payments/"
cp "$SRC/presentation/ui/screens/lightning/setup/"*.kt    "$DEST/presentation/ui/screens/lightning/setup/"

info "Copie MandacaruApplication.kt..."
cp "$SRC/MandacaruApplication.kt" "$DEST/"

info "Copie navigation (Destinations, MainActivity)..."
cp "$SRC/presentation/ui/screens/main/Destinations.kt" "$DEST/presentation/ui/screens/main/"
cp "$SRC/presentation/ui/screens/main/MainActivity.kt"  "$DEST/presentation/ui/screens/main/"

# ── Dépendances build.gradle.kts ──────────────────────────────────────────────
info "Ajout des dépendances dans app/build.gradle.kts..."

GRADLE="app/build.gradle.kts"
if grep -q "ldk-node-android" "$GRADLE"; then
    warn "ldk-node-android déjà présent dans build.gradle.kts — skipping."
else
    # Insère après la ligne JNA (qui existe déjà dans Mandacaru)
    sed -i '/implementation(libs.jna)/a\
\
    // ── Lightning Network ────────────────────────────────────────────────\
    implementation("org.lightningdevkit:ldk-node-android:0.7.0")\
    implementation("androidx.security:security-crypto:1.1.0-alpha06")\
    implementation("com.google.zxing:core:3.5.3")' "$GRADLE"
    info "Dépendances ajoutées."
fi

# ── Résumé ────────────────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${GREEN}  Lightning Network appliqué avec succès !             ${NC}"
echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "Prochaines étapes :"
echo "  1. Ajouter l'icône ic_bolt dans res/drawable/"
echo "  2. ./gradlew assembleDebug"
echo ""
