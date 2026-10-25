# HW #2 - clasificador de gestos con el sensor de luz (Edge Impulse, modo experto de Keras)
# Se pega entero en: Classifier -> menu de los 3 puntos -> "Switch to Keras (expert) mode".
# Entrada: 1 s de luz a 100 Hz (100 valores del bloque Raw Data). Salida: rock / paper / scissors.
#
# OJO, parpadeo de las luces: en Taiwan (red de 60 Hz) los tubos/LEDs parpadean a 120 Hz. El sensor lo ve,
# y al muestrear aparece "plegado" (aliasing): a 62.5 Hz cae en |120 - 2*62.5| = 5 Hz, justo donde estan
# los gestos, y en los primeros datos era 2/3 de la senal (modelo al 40%). A 100 Hz cae en 20 Hz, y la
# primera capa (promedio de 5 muestras = 50 ms) lo anula exactamente sin borrar las sombras de la mano.
#
# Mejoras sobre el modelo por defecto (dos capas Dense de 20 y 10 neuronas):
#  - red convolucional 1D: aprende la FORMA de las sombras (una pasada larga = piedra,
#    dos cortas = tijera, muchas = papel) sin importar en que momento del segundo paso la mano
#  - data augmentation: en cada epoca el modelo ve versiones distintas de cada muestra
#    (corrida en el tiempo, con mas o menos luz, con ruido). Con ~100 muestras grabadas en una
#    sala de clase, es lo que mas ayuda a que funcione con otra luz y otra mano
#  - cada muestra se centra en su propio nivel de luz (se le resta su media): la Wio no siempre
#    apunta igual y la luz de fondo variaba de 184 a 317 entre muestras; sin centrar, la red se
#    fijaba en eso y no en la forma de la sombra (kNN de prueba: 45% crudo vs 64% centrado)
#  - early stopping + bajar el learning rate: entrena hasta que el loss de validacion deja de
#    mejorar y se queda con los pesos de la mejor epoca (no los de la ultima)
import tensorflow as tf
from tensorflow.keras.models import Model
from tensorflow.keras.layers import (Input, Dense, Dropout, Conv1D, Reshape, MaxPooling1D,
                                     Subtract, Rescaling, GlobalAveragePooling1D)
from tensorflow.keras.optimizers.legacy import Adam
from tensorflow.keras.callbacks import EarlyStopping, ReduceLROnPlateau

EPOCHS = 400           # tope: el early stopping corta antes
LEARNING_RATE = 0.002
BATCH_SIZE = 16        # pocos datos: lotes chicos = mas pasos de entrenamiento por epoca
ENSURE_DETERMINISM = args.ensure_determinism
MAX_CORRIMIENTO = input_length // 8   # cuanto se puede correr el gesto en el tiempo (~125 ms)
FILTRO = 5             # muestras del promedio anti-parpadeo (5 a 100 Hz = 50 ms -> anula 20 Hz)


def aumentar(x, y):
    """Version distinta de la muestra en cada epoca."""
    corrimiento = tf.random.uniform([], -MAX_CORRIMIENTO, MAX_CORRIMIENTO + 1, dtype=tf.int32)
    x = tf.roll(x, corrimiento, axis=0)                                      # gesto antes / despues
    x = x * tf.random.uniform([], 0.75, 1.25)                                # sala mas clara / oscura
    x = x + tf.random.normal(tf.shape(x), stddev=0.01) * tf.reduce_mean(x)   # ruido del sensor
    return x, y


if not ENSURE_DETERMINISM:
    train_dataset = train_dataset.shuffle(buffer_size=BATCH_SIZE * 8)
train_dataset = train_dataset.map(aumentar)
train_dataset = train_dataset.batch(BATCH_SIZE, drop_remainder=False)
validation_dataset = validation_dataset.batch(BATCH_SIZE, drop_remainder=False)

# Modelo funcional (no Sequential) porque el centrado necesita dos ramas: la senal y su media
entrada = Input(shape=(input_length,), name='x_input')
x = Reshape((input_length, 1))(entrada)
# 1) centrar: restar el nivel medio de luz de la ventana (capas estandar: MEAN + SUB en la Wio)
media = GlobalAveragePooling1D(keepdims=True, name='media_luz')(x)
x = Subtract(name='centrar')([x, media])
x = Rescaling(10.0, name='escalar')(x)   # las sombras quedan en el orden de +-1
# 2) filtro fijo (no se entrena): promedio movil que elimina el parpadeo de las luces
x = Conv1D(1, kernel_size=FILTRO, padding='same', use_bias=False, trainable=False,
           kernel_initializer=tf.keras.initializers.Constant(1.0 / FILTRO), name='filtro_parpadeo')(x)
# 3) convoluciones que detectan caidas de luz de distinto largo
x = Conv1D(16, kernel_size=7, padding='same', activation='relu')(x)
x = MaxPooling1D(pool_size=2, padding='same')(x)
x = Conv1D(24, kernel_size=5, padding='same', activation='relu')(x)
x = MaxPooling1D(pool_size=2, padding='same')(x)
x = Conv1D(32, kernel_size=3, padding='same', activation='relu')(x)
# 4) promedio global: no importa DONDE cae la sombra, si COMO es
x = GlobalAveragePooling1D()(x)
x = Dropout(0.3)(x)
x = Dense(16, activation='relu')(x)
salida = Dense(classes, name='y_pred', activation='softmax')(x)
model = Model(inputs=entrada, outputs=salida)

opt = Adam(learning_rate=LEARNING_RATE, beta_1=0.9, beta_2=0.999)
callbacks.append(BatchLoggerCallback(BATCH_SIZE, train_sample_count, epochs=EPOCHS, ensure_determinism=ENSURE_DETERMINISM))
callbacks.append(EarlyStopping(monitor='val_loss', patience=80, restore_best_weights=True))
callbacks.append(ReduceLROnPlateau(monitor='val_loss', factor=0.5, patience=30, min_lr=1e-5))

model.compile(loss='categorical_crossentropy', optimizer=opt, metrics=['accuracy'])
model.fit(train_dataset, epochs=EPOCHS, validation_data=validation_dataset, verbose=2, callbacks=callbacks)

# Cuantizacion por canal (default de Edge Impulse): mejor precision en las convoluciones
disable_per_channel_quantization = False
