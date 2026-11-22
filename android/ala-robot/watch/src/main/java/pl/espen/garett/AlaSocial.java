package pl.espen.garett;

import static pl.espen.garett.AlaBehavior.Mood.*;
import java.text.Normalizer;
import java.util.Locale;

/** Explicit user STT and touch reactions from the P4 social policy. */
final class AlaSocial {
    private AlaSocial(){}

    enum Reaction {
        NONE(null,null), COMFORT(LOVE,LOVE), APOLOGY(HAPPY,HAPPY), ANNOYED(ANGRY,ANGRY),
        AFFECTION(LOVE,LOVE), PRAISE(SURPRISE,HAPPY), CELEBRATION(WINK,EXCITED),
        THANKS(HAPPY,HAPPY), GOODNIGHT(SLEEPY,SLEEPY), GREETING(HAPPY,HAPPY);

        final AlaBehavior.Mood initial,afterglow;
        Reaction(AlaBehavior.Mood initial,AlaBehavior.Mood afterglow){
            this.initial=initial;
            this.afterglow=afterglow;
        }
    }

    static String words(String value){
        if (value==null) return " ";
        String ascii=Normalizer.normalize(value.toLowerCase(Locale.ROOT),Normalizer.Form.NFD)
            .replaceAll("\\p{M}+","").replace('ł','l').replaceAll("[^a-z0-9]+"," ").trim();
        return ascii.isEmpty() ? " " : " "+ascii+" ";
    }

    static Reaction classify(String text){
        if (text==null || text.length()>512) return Reaction.NONE;
        String textWords=words(text);
        if (has(textWords,"powiedz","co znaczy","co oznacza","jak powiedziec","przetlumacz","cytat","czy"))
            return Reaction.NONE;
        if (has(textWords,"jest mi smutno","jestem smutny","jestem smutna","jestem chory","jestem chora",
                "jestem zmeczony","jestem zmeczona","jest mi zle","mam zly dzien","jestem zly","jestem zla",
                "zle sie czuje","nie czuje sie dobrze")) return Reaction.COMFORT;
        if (has(textWords,"przepraszam","wybacz mi","sorry")) return Reaction.APOLOGY;
        if (has(textWords,"nie lubie cie","jestes glupi","jestes glupia","jestes okropna","jestes okropny",
                "jestes wredna","jestes wredny","nienawidze cie","ale jestes nudna","ale jestes nudny"))
            return Reaction.ANNOYED;
        if (has(textWords,"nie","nienawidze","not")) return Reaction.NONE;
        if (has(textWords,"kocham cie","lubie cie","uwielbiam cie","jestes kochana","jestes kochany",
                "jestes slodka","jestes slodki","jestes urocza","jestes uroczy","jestes cudowna","jestes cudowny",
                "tesknilem za toba","tesknilam za toba","love you","przytul mnie","daj mi przytulasa",
                "chce sie przytulic","przytulam cie","buziak")) return Reaction.AFFECTION;
        if (has(textWords,"jestes super","jestes swietna","jestes swietny","jestes wspaniala","jestes wspanialy",
                "jestes najlepsza","jestes najlepszy","dobra robota","swietna robota","dobrze ci idzie",
                "ladnie wygladasz","piekna buzia","ale jestes fajna","ale jestes fajny","jestes fajna","jestes fajny",
                "jestes madra","jestes madry","super robota","well done","jestes naprawde swietna",
                "jestes naprawde swietny","jestes niesamowita","jestes niesamowity")
                || textWords.equals(" super ") || textWords.equals(" swietnie ")
                || textWords.equals(" ekstra ") || textWords.equals(" pieknie ")) return Reaction.PRAISE;
        if (has(textWords,"brawo","gratuluje","przybij piatke","high five")) return Reaction.CELEBRATION;
        if (has(textWords,"dziekuje","dzieki","thank you")) return Reaction.THANKS;
        if (has(textWords,"dobranoc","slodkich snow","good night")) return Reaction.GOODNIGHT;
        if (has(textWords,"dzien dobry","milo cie widziec","czesc ala","czesc espen","hej ala","hej espen","good morning"))
            return Reaction.GREETING;
        return Reaction.NONE;
    }

    private static boolean has(String words,String... phrases){
        for (String phrase:phrases) if (words.contains(" "+phrase+" ")) return true;
        return false;
    }

    static final class PetMotion {
        private float x,y,dx,dy,distance;
        private int reversals;
        private long start;

        void start(float x,float y,long now){
            this.x=x;this.y=y;
            dx=dy=distance=0;
            reversals=0;
            start=now;
        }

        AlaBehavior.Mood move(float x,float y,long now){
            float xStep=x-this.x,yStep=y-this.y;
            float step=Math.abs(xStep)+Math.abs(yStep);
            if (step>=4) {
                distance+=step;
                this.x=x;this.y=y;
                if (step>=8) {
                    if (xStep*dx+yStep*dy < -12) reversals++;
                    dx=xStep;dy=yStep;
                }
            }
            if (distance>=75 && reversals>=3 && now-start<=2600) return LAUGH;
            return distance>=24 ? LOVE : null;
        }
    }
}
