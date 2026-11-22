package pl.espen.garett;

import static pl.espen.garett.AlaBehavior.Mood.*;

/** Sentence rules follow visual_world.h and response_reactions.h at P4 c5e0040. */
final class NarrationCue {
    enum Scene {
        None(""),
        Saturn("saturn*"),
        Earth("planeta ziemia|ziemia|ziemie|ziemi|ziemia"),
        Mars("mars|marsa|marsie|marsem"),
        Moon("ksiezyc*"),
        Sun("slonce|slonca|sloncu|sloneczn*"),
        Rocket("rakiet*|kosmonaut*|astronaut*|start rakiety"),
        BlackHole("czarna dziura|czarnej dziur*|czarne dziur*"),
        Space("kosmos*|galaktyk*|gwiazd*|uklad sloneczny|planet*"),
        Dino("dinozaur*|tyranozaur*|triceratops*|jurajsk*"),
        Asteroid("asteroid*|meteoryt*|meteor*"),
        Volcano("wulkan*|lawa|lawy|lawe|lawie"),
        Cat("kot|kota|koty|kotem|kotku|kotek|kotki|kocie"),
        Dog("pies|psa|psu|psy|psem|piesek|pieski|szczeniak*"),
        Rabbit("krolik*|zajac*"),
        Lion("lew|lwa|lwy|lwem|grzywa"),
        Frog("zaba|zaby|zabe|zabie|zabka|zabki"),
        Bird("ptak*|ptaszek|ptaszki|papug*|orzel|orla|wrobel|wrobl*"),
        Bee("pszczol*|miod*|ul|ule"),
        Butterfly("motyl*|gasienic*"),
        Spider("pajak*|pajeczyn*"),
        Fish("ryb*|akwar*"),
        Shark("rekin*"),
        Octopus("osmiornic*|mack*"),
        Penguin("pingwin*"),
        Elephant("slon|slonia|slonie|sloniem|traba"),
        Snake("waz|weza|weze|wezem|pyton*"),
        Forest("las|lasu|lesie|drzew*|puszcza"),
        Flower("kwiat*|roslin*|nasion*|kielk*|fotosyntez*"),
        Rain("deszcz*|opad*|kropl*|parow*"),
        Snow("snieg*|sniezn*|platki sniegu|mroz*"),
        Storm("burz*|blyskawic*|piorun*|grzmot*"),
        Rainbow("tecz*|rozszczep*"),
        Ice("lod|lodu|lodem|lody|topnie*|zamarz*"),
        Ship("statek|statki|statku|statkiem|zaglow*|lodz|lodzie"),
        Plane("samolot*|lotnictw*|skrzydl*"),
        Car("samochod*|auto|auta|pojazd*"),
        Electric("elektrycz*|prad|pradu|elektron*|napiecie"),
        Magnet("magnes*|magnetyzm*|biegun*"),
        Heart("serce|serca|sercu|sercem|krew|krwi|tetno"),
        Lungs("pluc*|oddech*|oddych*"),
        Microbe("bakter*|wirus*|mikrob*|mikroorganizm*"),
        Engine("silnik*|tlok*|mechanizm*|przekladni*"),
        Robot("robot*|sztuczna inteligencja|sztucznej inteligencji|algorytm*|neuron*|komputer*|procesor*|stackchan|espen|emo"),
        Poland("polska|polski|polsce|polske|polakom|polak*"),
        Germany("niemcy|niemiec|niemczech|niemieck*"),
        Ukraine("ukrain*"),
        France("francj*|francusk*"),
        Italy("wlochy|wloszech|wlosk*"),
        Japan("japon*|japonsk*"),
        USA("usa|stany zjednoczone|stanach zjednoczonych|ameryk*"),
        Egypt("egipt*|piramid*|faraon*"),
        Castle("zamek|zamku|zamki|zamkiem|rycerz*|sredniowiecz*"),
        Mountains("gory|gora|gorach|gorski*|szczyt*|tatry|himalaj*"),
        Ocean("ocean*|morze|morza|morzem|morz*"),
        Desert("pustyn*|wydm*|sahara"),
        Dragon("smok*|smoczy*"),
        Crown("krol|krola|krolowi|krolowa|krolowej|koron*|ksieznicz*"),
        Magic("magi*|czarodzie*|czarownic*|rozdzk*|zakle*"),
        Pirate("pirat*|skarb*"),
        Ghost("duch|ducha|duchy|duchem|duszek"),
        Detective("zagadk*|detektyw*|lup*|tajemnic*"),
        Birthday("urodzin*|tort*|swieczk*|balon*"),
        Food("jablk*|banan*|pizza|pizze|kanapk*|jedzeni*|glod*|gotowa*|kuchar*"),
        Music("muzyk*|piosenk*|nut*|instrument*|spiew*|gitar*"),
        Sport("sport*|pilk*|mecz*|medal*|puchar*|wygral*"),
        Minecraft("minecraft*|creeper*|diament*|kilof*"),
        Lego("lego|klock*|klocek"),
        Clock("zegar*|godzin*|minut*|sekund*"),
        Fraction("ulam*|polowa|polow*|cwiartk*|mianownik*"),
        Geometry("geometri*|kwadrat*|trojkat*|prostokat*|obwod*|figur*|symetri*"),
        Math("matematyk*|dodawa*|odejm*|mnoz*|dziel*|rownani*|pierwiast*|liczb*|poteg*|procent*"),
        Success("brawo|gratul*|udalo sie|swietnie|doskonale|poprawnie|dobra robota"),
        Love("kocham|przytul*|serduszk*|sympati*|milosc"),
        Sleep("dobranoc|sen|snu|spac|spani*|senn*|ziew*"),
        Story("bajk*|basn*|opowiesc*|dawno dawno temu"),
        Talk("");

        final String[] words;
        Scene(String rules){words=rules.isEmpty()?new String[0]:rules.split("\\|");}
    }

    private static final class Beat {
        final AlaBehavior.Mood mood;
        final String[] phrases;
        Beat(AlaBehavior.Mood mood,String rules){
            this.mood=mood;
            phrases=rules.split("\\|");
        }
    }
    private static final Scene[] SCENES=Scene.values();
    private static final Beat[] BEATS={
        new Beat(CONFUSED, "ups|oops|moja pomylka|ojej pomylilem|ojej pomylilam"),
        new Beat(SHY, "psst|nie mow nikomu|miedzy nami|zdradze ci sekret|to sekret"),
        new Beat(WINK, "abrakadabra|czary mary|hokus pokus|magiczna sztuczka"),
        new Beat(EXCITED, "brawo|gratulacje|udalo sie|swietna robota|jestem z ciebie dumna|jestem z ciebie dumny"),
        new Beat(LAUGH, "haha|ha ha|hihi|hehe|hahaha|zartuje"),
        new Beat(SURPRISE, "wow|o rety|nie do wiary|ale niespodzianka|niesamowite"),
        new Beat(LOVE, "przytulam cie|jestem przy tobie|nie martw sie|dasz rade|jestem z toba"),
        new Beat(THINKING, "zgadnij|mam zagadke|zagadka dla ciebie|jak myslisz|pomysl przez chwile"),
        new Beat(EXCITED, "tadam|ta dam|werble prosze|czas na werble"),
        new Beat(EXCITED, "zatańczmy|zatanczmy|tanczymy|czas na taniec|taniec zwyciestwa|zrobmy taniec"),
        new Beat(BORED, "taaa jasne|no tak oczywiscie|przewracam oczami|serio mowisz|no jasne ze nie"),
        new Beat(CONFIDENT, "hurra|hura|jupi|wygralismy|zwyciestwo|jestesmy zwyciezcami"),
        new Beat(SURPRISE, "eureka|juz wiem|mam pomysl|olśnienie|olsnienie|rozgryzlam to|rozgryzlem to"),
        new Beat(THINKING, "zaraz sie okaze|wielki final|napiecie rosnie|chwila prawdy|trzymaj sie mocno"),
        new Beat(WINK, "psikus|mam sprytny plan|podstepny plan|zrobie ci psikusa|maly zarcik"),
        new Beat(SAD, "przykro mi|to smutne|ale szkoda|bardzo mi przykro"),
        new Beat(SLEEPY, "dobranoc|slodkich snow|czas spac|pora na sen"),
        new Beat(THINKING, "sledztwo|znajdzmy trop|szukajmy wskazowek|jestem detektywem|czas na dochodzenie"),
        new Beat(CONFIDENT, "bip bop|beep boop|tryb robota|robotyczny glos"),
        new Beat(HAPPY, "czesc|hej|witaj|dzien dobry|milo cie poznac"),
        new Beat(EXCITED, "ciesze sie|super sprawa|ale wspaniale"),
        new Beat(LOVE, "kocham cie|bardzo cie lubie|wysylam serduszko"),
        new Beat(THINKING, "to ciekawe|ciekawe pytanie"),
        new Beat(HAPPY, "masz racje|dokladnie tak|zgadza sie|pelna zgoda")
    };

    private NarrationCue(){}

    static Scene scene(String text){
        if (text==null) return null;
        String words=AlaSocial.words(text.substring(0,Math.min(1024,text.length())));
        for (Scene scene:SCENES) if (matches(words,scene.words)) return scene;
        return null;
    }

    static AlaBehavior.Mood mood(String text){
        if (text==null) return NEUTRAL;
        String words=AlaSocial.words(text.substring(0,Math.min(360,text.length())));
        for (Beat beat:BEATS) if (matches(words,beat.phrases)) return beat.mood;
        if (matches(words,SAD_STORY)) return SAD;
        if (matches(words,SURPRISED_STORY)) return SURPRISE;
        if (matches(words,CURIOUS_STORY)) return THINKING;
        if (matches(words,HAPPY_STORY)) return HAPPY;
        if (matches(words,WARM_STORY)) return LOVE;
        return NEUTRAL;
    }

    private static final String[] SAD_STORY={"zasmu*","rozplakal*","smutn*","ze smutkiem"};
    private static final String[] SURPRISED_STORY={"ku zaskoczeniu","nieoczekiwanie","ku zdumieniu","az podskocz*"};
    private static final String[] CURIOUS_STORY={"tajemnic*","z ciekawoscia","zagadkow*"};
    private static final String[] HAPPY_STORY={"uradowan*","z radoscia","ze szczescia","rozesmial*"};
    private static final String[] WARM_STORY={"przytulil*","cieplo sie usmiechn*","dobry przyjaciel"};

    private static boolean matches(String words,String[] rules){
        for (String rule:rules) {
            String pattern=" "+(rule.endsWith("*")?rule.substring(0,rule.length()-1):rule+" ");
            if (words.contains(pattern)) return true;
        }
        return false;
    }
}
