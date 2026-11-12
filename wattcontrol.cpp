// WattControl - gestor de energia de una panaderia (traduccion de AquaControl)
// Clases: Carga (abstracta), CargaCritica, CargaAplazable, Medidor, Mercado, Negocio
#include <iostream>
#include <iomanip>
#include <string>
#include <cstdlib>
#include <ctime>
#include <limits>
using namespace std;

#define HORAS_DIA 24

// ---------- Constantes de la panaderia ----------
const double POT_CAMARA        = 3.0;     // kW, carga critica
const double POT_HORNO         = 12.0;    // kW, aplazable
const double POT_AMASADORA     = 4.0;     // kW, aplazable
const double POT_CONTRATADA    = 15.0;    // kW
const double PRECIO_CARO       = 900.0;   // COP/kWh
const double PRECIO_EMERGENCIA = 1500.0;  // COP/kWh
const double NIVEL_ALERTA_EMBALSES = 40.0;// %
const int HORA_PICO_INI        = 18;
const int HORA_PICO_FIN        = 19;
const int HORAS_MIN_HORNO      = 6;
const int HORAS_MIN_AMASADORA  = 4;
const int FALLA_LECTURA_CADA   = 20;      // 1 de cada 20 horas
const int MAX_CARGAS           = 3;
const double PORC_MIN          = 0.0;
const double PORC_MAX          = 100.0;
const double EMBALSES_INICIAL  = 60.0;
const int PASO_EMBALSES        = 5;       // variacion maxima por hora
const double PRECIO_REFERENCIA = 500.0;   // si falla la lectura en la hora 0

// Franjas de precio (COP/kWh) segun la tabla de la clase
const int NUM_FRANJAS = 5;
const int FRANJA_HORA_INI[NUM_FRANJAS]   = {0, 6, 11, 16, 20};
const int PRECIO_MIN_FRANJA[NUM_FRANJAS] = {398, 499, 465, 650, 580};
const int PRECIO_MAX_FRANJA[NUM_FRANJAS] = {540, 642, 970, 1612, 970};

// ---------- Carga (clase base abstracta) ----------
class Carga {
protected:
    string nombre;
    double potenciaKw;
    bool encendida;
    int horasUso;
public:
    Carga(const string& n, double p) : nombre(n), potenciaKw(p), encendida(false), horasUso(0) {}
    virtual ~Carga() {}
    virtual bool debeEncender(double precio, double embalses, int hora) const = 0;
    virtual bool esAplazable() const { return false; }
    virtual bool cumpleMinimo() const { return true; }
    virtual int getHorasMinimas() const { return 0; }

    string getNombre() const { return nombre; }
    double getPotencia() const { return potenciaKw; }
    bool estaEncendida() const { return encendida; }
    int getHorasUso() const { return horasUso; }
    double getKwh() const { return potenciaKw * horasUso; }
    void encender() { encendida = true; }
    void apagar() { encendida = false; }
    void sumarHora() { horasUso++; }
    void reiniciar() { encendida = false; horasUso = 0; }
};

// ---------- CargaCritica ----------
class CargaCritica : public Carga {
public:
    CargaCritica(const string& n, double p) : Carga(n, p) {}
    bool debeEncender(double, double, int) const override { return true; }  // regla 1
};

// ---------- CargaAplazable ----------
class CargaAplazable : public Carga {
private:
    int horasMinimas;
public:
    CargaAplazable(const string& n, double p, int minimas) : Carga(n, p), horasMinimas(minimas) {}
    bool debeEncender(double precio, double embalses, int hora) const override {
        bool horaPico = (hora >= HORA_PICO_INI && hora <= HORA_PICO_FIN);
        bool aplazar = (embalses < NIVEL_ALERTA_EMBALSES) || horaPico || (precio > PRECIO_CARO);
        return !aplazar;
    }
    bool esAplazable() const override { return true; }
    bool cumpleMinimo() const override { return horasUso >= horasMinimas; }
    int getHorasMinimas() const override { return horasMinimas; }
};

// ---------- Medidor (parte del Negocio: composicion) ----------
class Medidor {
private:
    double kwhTotal;
    double costoTotal;
public:
    Medidor() : kwhTotal(0), costoTotal(0) {}
    void registrar(double kw, double precio) { kwhTotal += kw; costoTotal += kw * precio; }
    double getKwh() const { return kwhTotal; }
    double getCosto() const { return costoTotal; }
    void reiniciar() { kwhTotal = 0; costoTotal = 0; }
};

// ---------- Mercado (asociacion con Negocio) ----------
class Mercado {
private:
    double embalses;
    int franjaDe(int hora) const {
        int f = 0;
        for (int i = 0; i < NUM_FRANJAS; i++)
            if (hora >= FRANJA_HORA_INI[i]) f = i;
        return f;
    }
public:
    Mercado() : embalses(EMBALSES_INICIAL) {}
    double precioHora(int hora) const {
        int f = franjaDe(hora);
        return PRECIO_MIN_FRANJA[f] + rand() % (PRECIO_MAX_FRANJA[f] - PRECIO_MIN_FRANJA[f] + 1);
    }
    double getEmbalses() const { return embalses; }
    void setEmbalses(double e) { embalses = e; }
    void variarEmbalses() {
        embalses += rand() % (2 * PASO_EMBALSES + 1) - PASO_EMBALSES;
        if (embalses < PORC_MIN) embalses = PORC_MIN;
        if (embalses > PORC_MAX) embalses = PORC_MAX;
    }
};

// ---------- Negocio ----------
class Negocio {
private:
    Carga* cargas[MAX_CARGAS];      // agregacion: se crean afuera
    int numCargas;
    Medidor medidor;                // composicion
    Mercado* mercado;               // asociacion
    double costoSinGestor;
    int horasSimuladas;
    bool hayResumen, emergencia, produccionIncompleta;

    double potenciaEncendida() const {
        double s = 0;
        for (int i = 0; i < numCargas; i++)
            if (cargas[i]->estaEncendida()) s += cargas[i]->getPotencia();
        return s;
    }
    double potenciaTotal() const {
        double s = 0;
        for (int i = 0; i < numCargas; i++) s += cargas[i]->getPotencia();
        return s;
    }
    void acumular(double precio) {
        for (int i = 0; i < numCargas; i++)
            if (cargas[i]->estaEncendida()) {
                cargas[i]->sumarHora();
                medidor.registrar(cargas[i]->getPotencia(), precio);
            }
        costoSinGestor += potenciaTotal() * precio;
    }
    void imprimirEstados() const {
        for (int i = 0; i < numCargas; i++)
            cout << cargas[i]->getNombre() << ": " << (cargas[i]->estaEncendida() ? "ON " : "OFF") << "  ";
        cout << endl;
    }
public:
    Negocio(Mercado* m) : numCargas(0), mercado(m), costoSinGestor(0), horasSimuladas(0),
                          hayResumen(false), emergencia(false), produccionIncompleta(false) {}

    bool agregarCarga(Carga* c) {
        if (numCargas >= MAX_CARGAS) return false;
        cargas[numCargas++] = c;
        return true;
    }

    // El mismo ciclo le pregunta debeEncender() a todas; luego aplica la regla 6
    void decidir(int hora, double precio, double embalses) {
        for (int i = 0; i < numCargas; i++) {
            if (cargas[i]->debeEncender(precio, embalses, hora)) cargas[i]->encender();
            else cargas[i]->apagar();
        }
        while (potenciaEncendida() > POT_CONTRATADA) {
            int idx = -1;
            for (int i = 0; i < numCargas; i++)
                if (cargas[i]->esAplazable() && cargas[i]->estaEncendida() &&
                    (idx == -1 || cargas[i]->getPotencia() > cargas[idx]->getPotencia()))
                    idx = i;
            if (idx == -1) break;
            cargas[idx]->apagar();
            cout << "  ALERTA: se supera la potencia contratada (" << POT_CONTRATADA
                 << " kW). Se apaga " << cargas[idx]->getNombre() << "." << endl;
        }
    }

    void evaluar(int hora, double precio, double embalses) {
        cout << "\nHora " << hora << " | precio " << precio << " COP/kWh | embalses " << embalses << "%" << endl;
        decidir(hora, precio, embalses);
        for (int i = 0; i < numCargas; i++)
            cout << "  " << cargas[i]->getNombre() << ": "
                 << (cargas[i]->estaEncendida() ? "SE ENCIENDE" : "SE APLAZA") << endl;
        cout << "  Potencia encendida: " << potenciaEncendida() << " kW" << endl;
    }

    void simularDia() {
        for (int i = 0; i < numCargas; i++) cargas[i]->reiniciar();
        medidor.reiniciar();
        costoSinGestor = 0; horasSimuladas = 0;
        emergencia = false; produccionIncompleta = false;
        double ultimoPrecio = PRECIO_REFERENCIA;

        cout << "\n=== Simulacion de " << HORAS_DIA << " horas ===" << endl;
        for (int hora = 0; hora < HORAS_DIA; hora++) {
            mercado->variarEmbalses();
            double emb = mercado->getEmbalses();

            if (rand() % FALLA_LECTURA_CADA == 0) {   // falla la lectura del precio
                for (int i = 0; i < numCargas; i++) {
                    if (!cargas[i]->esAplazable()) cargas[i]->encender();
                    else cargas[i]->apagar();
                }
                acumular(ultimoPrecio);
                horasSimuladas++;
                cout << "H" << setw(2) << hora << " | FALLA DE LECTURA, solo carga critica | ";
                imprimirEstados();
                continue;
            }

            double precio = mercado->precioHora(hora);
            if (precio > PRECIO_EMERGENCIA && emb < NIVEL_ALERTA_EMBALSES) {
                emergencia = true;
                cout << "H" << setw(2) << hora << " | EMERGENCIA DE RACIONAMIENTO (precio " << precio
                     << ", embalses " << emb << "%). Fin de la simulacion." << endl;
                break;
            }
            ultimoPrecio = precio;
            decidir(hora, precio, emb);
            acumular(precio);
            horasSimuladas++;
            cout << "H" << setw(2) << hora << " | " << setw(5) << precio << " COP | emb "
                 << setw(3) << emb << "% | ";
            imprimirEstados();
        }
        for (int i = 0; i < numCargas; i++)
            if (!cargas[i]->cumpleMinimo()) produccionIncompleta = true;
        hayResumen = true;
        cout << "Simulacion terminada. Use la opcion 4 para ver el resumen." << endl;
    }

    void resumen() const {
        if (!hayResumen) { cout << "Aun no hay simulacion. Use la opcion 3." << endl; return; }
        cout << fixed << setprecision(2);
        cout << "\n=== Resumen de la ultima simulacion (" << horasSimuladas << " h) ===" << endl;
        for (int i = 0; i < numCargas; i++)
            cout << cargas[i]->getNombre() << ": " << cargas[i]->getHorasUso() << " h, "
                 << cargas[i]->getKwh() << " kWh" << endl;
        double con = medidor.getCosto();
        double ahorro = costoSinGestor - con;
        double pct = (costoSinGestor > 0) ? ahorro / costoSinGestor * 100.0 : 0.0;
        cout << "Costo con gestor: " << con << " COP" << endl;
        cout << "Costo sin gestor: " << costoSinGestor << " COP" << endl;
        cout << "Ahorro: " << ahorro << " COP (" << pct << " %)" << endl;
        if (emergencia) cout << "Hubo emergencia de racionamiento." << endl;
        if (produccionIncompleta) cout << "ALERTA: produccion incompleta" << endl;
        cout << defaultfloat;
    }
};

// ---------- Validacion de entradas ----------
double leerNumero() {
    double v;
    while (!(cin >> v)) {
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        cout << "Entrada no numerica, intente de nuevo: ";
    }
    return v;
}
int leerHora() {
    int h;
    do {
        cout << "Hora (0-" << HORAS_DIA - 1 << "): ";
        h = (int)leerNumero();
        if (h < 0 || h > HORAS_DIA - 1) cout << "Hora invalida." << endl;
    } while (h < 0 || h > HORAS_DIA - 1);
    return h;
}
double leerPrecio() {
    double p;
    do {
        cout << "Precio (COP/kWh, mayor que 0): ";
        p = leerNumero();
        if (p <= 0) cout << "Precio invalido." << endl;
    } while (p <= 0);
    return p;
}
double leerEmbalses() {
    double e;
    do {
        cout << "Nivel de embalses (" << PORC_MIN << "-" << PORC_MAX << " %): ";
        e = leerNumero();
        if (e < PORC_MIN || e > PORC_MAX) cout << "Nivel invalido." << endl;
    } while (e < PORC_MIN || e > PORC_MAX);
    return e;
}

// ---------- Programa principal ----------
int main() {
    srand((unsigned)time(0));
    Mercado mercado;
    Negocio negocio(&mercado);
    CargaCritica camara("Camara", POT_CAMARA);
    CargaAplazable horno("Horno", POT_HORNO, HORAS_MIN_HORNO);
    CargaAplazable amasadora("Amasadora", POT_AMASADORA, HORAS_MIN_AMASADORA);
    negocio.agregarCarga(&camara);
    negocio.agregarCarga(&horno);
    negocio.agregarCarga(&amasadora);

    int opcion;
    bool hayLectura = false;
    int hora = 0; double precio = 0, embalses = 0;

    do {
        cout << "\n===== WattControl - Panaderia =====\n"
             << "1. Ingresar lectura manual (hora, precio, embalses)\n"
             << "2. Evaluar decision de cada carga\n"
             << "3. Simular un dia completo (24 h)\n"
             << "4. Ver resumen de la ultima simulacion\n"
             << "0. Salir\nOpcion: ";
        if (!(cin >> opcion)) {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            opcion = -1;
        }
        switch (opcion) {
            case 1:
                hora = leerHora();
                precio = leerPrecio();
                embalses = leerEmbalses();
                mercado.setEmbalses(embalses);
                hayLectura = true;
                break;
            case 2:
                if (!hayLectura) cout << "Primero ingrese una lectura (opcion 1)." << endl;
                else negocio.evaluar(hora, precio, embalses);
                break;
            case 3:
                negocio.simularDia();
                break;
            case 4:
                negocio.resumen();
                break;
            case 0:
                cout << "Hasta pronto." << endl;
                break;
            default:
                cout << "Opcion invalida." << endl;
        }
    } while (opcion != 0);
    return 0;
}
