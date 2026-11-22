package pl.espen.garett;

/** Projects the current game and transport state onto both controls and handlers. */
final class AlaInteraction {
    enum Action { GAMES, CAMERA, MICROPHONE, CHOICE, SETTINGS }

    private AlaInteraction(){}

    static boolean allows(Action action,boolean foreground,boolean permissionPending,
                          boolean storyTurn,boolean cameraTurn,boolean busy) {
        if (!foreground || permissionPending || storyTurn || cameraTurn) return false;
        return !busy || action==Action.MICROPHONE;
    }
}
