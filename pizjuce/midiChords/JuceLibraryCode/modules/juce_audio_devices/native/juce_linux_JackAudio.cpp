/*
  ==============================================================================

   This file is part of the JUCE library - "Jules' Utility Class Extensions"
   Copyright 2004-11 by Raw Material Software Ltd.

  ------------------------------------------------------------------------------

   JUCE can be redistributed and/or modified under the terms of the GNU General
   Public License (Version 2), as published by the Free Software Foundation.
   A copy of the license is included in the JUCE distribution, or can be found
   online at www.gnu.org/licenses.

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.rawmaterialsoftware.com/juce for more information.

  ==============================================================================
*/

//==============================================================================
static void* juce_libjackHandle = nullptr;

static void* juce_loadJackFunction (const char* const name)
{
    if (juce_libjackHandle == nullptr)
        return nullptr;

    return dlsym (juce_libjackHandle, name);
}

#define JUCE_DECL_JACK_FUNCTION(return_type, fn_name, argument_types, arguments)  \
  typedef return_type (*fn_name##_ptr_t)argument_types;                           \
  return_type fn_name argument_types {                                            \
    static fn_name##_ptr_t fn = nullptr;                                          \
    if (fn == nullptr) { fn = (fn_name##_ptr_t)juce_loadJackFunction(#fn_name); } \
    if (fn) return (*fn)arguments;                                                \
    else return nullptr;                                                          \
  }

#define JUCE_DECL_VOID_JACK_FUNCTION(fn_name, argument_types, arguments)          \
  typedef void (*fn_name##_ptr_t)argument_types;                                  \
  void fn_name argument_types {                                                   \
    static fn_name##_ptr_t fn = nullptr;                                          \
    if (fn == nullptr) { fn = (fn_name##_ptr_t)juce_loadJackFunction(#fn_name); } \
    if (fn) (*fn)arguments;                                                       \
  }

//==============================================================================
JUCE_DECL_JACK_FUNCTION (jack_client_t*, jack_client_open, (const char* client_name, jack_options_t options, jack_status_t* status, ...), (client_name, options, status));
JUCE_DECL_JACK_FUNCTION (int, jack_client_close, (jack_client_t *client), (client));
JUCE_DECL_JACK_FUNCTION (int, jack_activate, (jack_client_t* client), (client));
JUCE_DECL_JACK_FUNCTION (int, jack_deactivate, (jack_client_t* client), (client));
JUCE_DECL_JACK_FUNCTION (jack_nframes_t, jack_get_buffer_size, (jack_client_t* client), (client));
JUCE_DECL_JACK_FUNCTION (jack_nframes_t, jack_get_sample_rate, (jack_client_t* client), (client));
JUCE_DECL_VOID_JACK_FUNCTION (jack_on_shutdown, (jack_client_t* client, void (*function)(void* arg), void* arg), (client, function, arg));
JUCE_DECL_JACK_FUNCTION (void* , jack_port_get_buffer, (jack_port_t* port, jack_nframes_t nframes), (port, nframes));
JUCE_DECL_JACK_FUNCTION (jack_nframes_t, jack_port_get_total_latency, (jack_client_t* client, jack_port_t* port), (client, port));
JUCE_DECL_JACK_FUNCTION (jack_port_t* , jack_port_register, (jack_client_t* client, const char* port_name, const char* port_type, unsigned long flags, unsigned long buffer_size), (client, port_name, port_type, flags, buffer_size));
JUCE_DECL_VOID_JACK_FUNCTION (jack_set_error_function, (void (*func)(const char*)), (func));
JUCE_DECL_JACK_FUNCTION (int, jack_set_process_callback, (jack_client_t* client, JackProcessCallback process_callback, void* arg), (client, process_callback, arg));
JUCE_DECL_JACK_FUNCTION (const char**, jack_get_ports, (jack_client_t* client, const char* port_name_pattern, const char* type_name_pattern, unsigned long flags), (client, port_name_pattern, type_name_pattern, flags));
JUCE_DECL_JACK_FUNCTION (int, jack_connect, (jack_client_t* client, const char* source_port, const char* destination_port), (client, source_port, destination_port));
JUCE_DECL_JACK_FUNCTION (const char*, jack_port_name, (const jack_port_t* port), (port));
JUCE_DECL_JACK_FUNCTION (int, jack_set_port_connect_callback, (jack_client_t* client, JackPortConnectCallback connect_callback, void* arg), (client, connect_callback, arg));
JUCE_DECL_JACK_FUNCTION (jack_port_t* , jack_port_by_id, (jack_client_t* client, jack_port_id_t port_id), (client, port_id));
JUCE_DECL_JACK_FUNCTION (int, jack_port_connected, (const jack_port_t* port), (port));
JUCE_DECL_JACK_FUNCTION (int, jack_port_connected_to, (const jack_port_t* port, const char* port_name), (port, port_name));

#if JUCE_DEBUG
  #define JACK_LOGGING_ENABLED 1
#endif

#if JACK_LOGGING_ENABLED
namespace
{
    void jack_Log (const String& s)
    {
        std::cerr << s << std::endl;
    }

    void dumpJackErrorMessage (const jack_status_t status)
    {
        if (status & JackServerFailed || status & JackServerError)  jack_Log ("Unable to connect to JACK server");
        if (status & JackVersionError)      jack_Log ("Client's protocol version does not match");
        if (status & JackInvalidOption)     jack_Log ("The operation contained an invalid or unsupported option");
        if (status & JackNameNotUnique)     jack_Log ("The desired client name was not unique");
        if (status & JackNoSuchClient)      jack_Log ("Requested client does not exist");
        if (status & JackInitFailure)       jack_Log ("Unable to initialize client");
    }
}
#else
  #define dumpJackErrorMessage(a) {}
  #define jack_Log(...) {}
#endif


//==============================================================================
#ifndef JUCE_JACK_CLIENT_NAME
  #define JUCE_JACK_CLIENT_NAME "JuceJack"
#endif

//==============================================================================
class JackAudioIODevice   : public AudioIODevice
{
public:
    JackAudioIODevice (const String& deviceName,
                       const String& inputId_,
                       const String& outputId_)
        : AudioIODevice (deviceName, "JACK"),
          inputId (inputId_),
          outputId (outputId_),
          isOpen_ (false),
          callback (nullptr),
          totalNumberOfInputChannels (0),
          totalNumberOfOutputChannels (0)
    {
        jassert (deviceName.isNotEmpty());

        jack_status_t status;
        client = juce::jack_client_open (JUCE_JACK_CLIENT_NAME, JackNoStartServer, &status);

        if (client == 0)
        {
            dumpJackErrorMessage (status);
        }
        else
        {
            juce::jack_set_error_function (errorCallback);

            // open input ports
            const StringArray inputChannels (getInputChannelNames());
            for (int i = 0; i < inputChannels.size(); i++)
            {
                String inputName;
                inputName << "in_" << ++totalNumberOfInputChannels;

                inputPorts.add (juce::jack_port_register (client, inputName.toUTF8(),
                                                          JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0));
            }

            // open output ports
            const StringArray outputChannels (getOutputChannelNames());
            for (int i = 0; i < outputChannels.size (); i++)
            {
                String outputName;
                outputName << "out_" << ++totalNumberOfOutputChannels;

                outputPorts.add (juce::jack_port_register (client, outputName.toUTF8(),
                                                           JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0));
            }

            inChans.calloc (totalNumberOfInputChannels + 2);
            outChans.calloc (totalNumberOfOutputChannels + 2);
        }
    }

    ~JackAudioIODevice()
    {
        close();
        if (client != 0)
        {
            juce::jack_client_close (client);
            client = 0;
        }
    }

    StringArray getChannelNames (bool forInput) const
    {
        StringArray names;
        const char** const ports = juce::jack_get_ports (client, 0, 0, /* JackPortIsPhysical | */
                                                         forInput ? JackPortIsInput : JackPortIsOutput);

        if (ports != 0)
        {
            int j = 0;
            while (ports[j] != 0)
            {
                const String portName (ports [j++]);

                if (portName.upToFirstOccurrenceOf (":", false, false) == getName())
                    names.add (portName.fromFirstOccurrenceOf (":", false, false));
            }

            free (ports);
        }

        return names;
    }

    StringArray getOutputChannelNames()         { return getChannelNames (false); }
    StringArray getInputChannelNames()          { return getChannelNames (true); }
    int getNumSampleRates()                     { return client != 0 ? 1 : 0; }
    double getSampleRate (int index)            { return client != 0 ? juce::jack_get_sample_rate (client) : 0; }
    int getNumBufferSizesAvailable()            { return client != 0 ? 1 : 0; }
    int getBufferSizeSamples (int index)        { return getDefaultBufferSize(); }
    int getDefaultBufferSize()                  { return client != 0 ? juce::jack_get_buffer_size (client) : 0; }

    String open (const BigInteger& inputChannels, const BigInteger& outputChannels,
                 double sampleRate, int bufferSizeSamples)
    {
        if (client == 0)
        {
            lastError = "No JACK client running";
            return lastError;
        }

        lastError = String::empty;
        close();

        juce::jack_set_process_callback (client, processCallback, this);
        juce::jack_on_shutdown (client, shutdownCallback, this);
        juce::jack_activate (client);
        isOpen_ = true;

        if (! inputChannels.isZero())
        {
            const char** const ports = juce::jack_get_ports (client, 0, 0, /* JackPortIsPhysical | */ JackPortIsOutput);

            if (ports != 0)
            {
                const int numInputChannels = inputChannels.getHighestBit() + 1;

                for (int i = 0; i < numInputChannels; ++i)
                {
                    const String portName (ports[i]);

                    if (inputChannels[i] && portName.upToFirstOccurrenceOf (":", false, false) == getName())
                    {
                        int error = juce::jack_connect (client, ports[i], juce::jack_port_name ((jack_port_t*) inputPorts[i]));
                        if (error != 0)
                            jack_Log ("Cannot connect input port " + String (i) + " (" + String (ports[i]) + "), error " + String (error));
                    }
                }

                free (ports);
            }
        }

        if (! outputChannels.isZero())
        {
            const char** const ports = juce::jack_get_ports (client, 0, 0, /* JackPortIsPhysical | */ JackPortIsInput);

            if (ports != 0)
            {
                const int numOutputChannels = outputChannels.getHighestBit() + 1;

                for (int i = 0; i < numOutputChannels; ++i)
                {
                    const String portName (ports[i]);

                    if (outputChannels[i] && portName.upToFirstOccurrenceOf (":", false, false) == getName())
                    {
                        int error = juce::jack_connect (client, juce::jack_port_name ((jack_port_t*) outputPorts[i]), ports[i]);
                        if (error != 0)
                            jack_Log ("Cannot connect output port " + String (i) + " (" + String (ports[i]) + "), error " + String (error));
                    }
                }

                free (ports);
            }
        }

        return lastError;
    }

    void close()
    {
        stop();

        if (client != 0)
        {
            juce::jack_deactivate (client);
            juce::jack_set_process_callback (client, processCallback, 0);
            juce::jack_on_shutdown (client, shutdownCallback, 0);
        }

        isOpen_ = false;
    }

    void start (AudioIODeviceCallback* newCallback)
    {
        if (isOpen_ && newCallback != callback)
        {
            if (newCallback != nullptr)
                newCallback->audioDeviceAboutToStart (this);

            AudioIODeviceCallback* const oldCallback = callback;

            {
                const ScopedLock sl (callbackLock);
                callback = newCallback;
            }

            if (oldCallback != nullptr)
                oldCallback->audioDeviceStopped();
        }
    }

    void stop()
    {
        start (0);
    }

    bool isOpen()                           { return isOpen_; }
    bool isPlaying()                        { return callback != nullptr; }
    int getCurrentBufferSizeSamples()       { return getBufferSizeSamples (0); }
    double getCurrentSampleRate()           { return getSampleRate (0); }
    int getCurrentBitDepth()                { return 32; }
    String getLastError()                   { return lastError; }

    BigInteger getActiveOutputChannels() const
    {
        BigInteger outputBits;

        for (int i = 0; i < outputPorts.size(); i++)
            if (juce::jack_port_connected ((jack_port_t*) outputPorts [i]))
                outputBits.setBit (i);

        return outputBits;
    }

    BigInteger getActiveInputChannels() const
    {
        BigInteger inputBits;

        for (int i = 0; i < inputPorts.size(); i++)
            if (juce::jack_port_connected ((jack_port_t*) inputPorts [i]))
                inputBits.setBit (i);

        return inputBits;
    }

    int getOutputLatencyInSamples()
    {
        int latency = 0;

        for (int i = 0; i < outputPorts.size(); i++)
            latency = jmax (latency, (int) juce::jack_port_get_total_latency (client, (jack_port_t*) outputPorts [i]));

        return latency;
    }

    int getInputLatencyInSamples()
    {
        int latency = 0;

        for (int i = 0; i < inputPorts.size(); i++)
            latency = jmax (latency, (int) juce::jack_port_get_total_latency (client, (jack_port_t*) inputPorts [i]));

        return latency;
    }

    String inputId, outputId;

private:
    void process (const int numSamples)
    {
        int i, numActiveInChans = 0, numActiveOutChans = 0;

        for (i = 0; i < totalNumberOfInputChannels; ++i)
        {
            jack_default_audio_sample_t* in
                = (jack_default_audio_sample_t*) juce::jack_port_get_buffer ((jack_port_t*) inputPorts.getUnchecked(i), numSamples);

            if (in != nullptr)
                inChans [numActiveInChans++] = (float*) in;
        }

        for (i = 0; i < totalNumberOfOutputChannels; ++i)
        {
            jack_default_audio_sample_t* out
                = (jack_default_audio_sample_t*) juce::jack_port_get_buffer ((jack_port_t*) outputPorts.getUnchecked(i), numSamples);

            if (out != nullptr)
                outChans [numActiveOutChans++] = (float*) out;
        }

        const ScopedLock sl (callbackLock);

        if (callback != nullptr)
        {
            callback->audioDeviceIOCallback (const_cast <const float**> (inChans.getData()), numActiveInChans,
                                             outChans, numActiveOutChans, numSamples);
        }
        else
        {
            for (i = 0; i < numActiveOutChans; ++i)
                zeromem (outChans[i], sizeof (float) * numSamples);
        }
    }

    static int processCallback (jack_nframes_t nframes, void* callbackArgument)
    {
        if (callbackArgument != 0)
            ((JackAudioIODevice*) callbackArgument)->process (nframes);

        return 0;
    }

    static void threadInitCallback (void* callbackArgument)
    {
        jack_Log ("JackAudioIODevice::initialise");
    }

    static void shutdownCallback (void* callbackArgument)
    {
        jack_Log ("JackAudioIODevice::shutdown");

        JackAudioIODevice* device = (JackAudioIODevice*) callbackArgument;

        if (device != nullptr)
        {
            device->client = 0;
            device->close();
        }
    }

    static void errorCallback (const char* msg)
    {
        jack_Log ("JackAudioIODevice::errorCallback " + String (msg));
    }

    bool isOpen_;
    jack_client_t* client;
    String lastError;
    AudioIODeviceCallback* callback;
    CriticalSection callbackLock;

    HeapBlock <float*> inChans, outChans;
    int totalNumberOfInputChannels;
    int totalNumberOfOutputChannels;
    Array<void*> inputPorts, outputPorts;
};


//==============================================================================
class JackAudioIODeviceType  : public AudioIODeviceType
{
public:
    //==============================================================================
    JackAudioIODeviceType()
        : AudioIODeviceType ("JACK"),
          hasScanned (false)
    {
    }

    //==============================================================================
    void scanForDevices()
    {
        hasScanned = true;
        inputNames.clear();
        inputIds.clear();
        outputNames.clear();
        outputIds.clear();

        if (juce_libjackHandle == nullptr)
        {
            juce_libjackHandle = dlopen ("libjack.so", RTLD_LAZY);

            if (juce_libjackHandle == nullptr)
                return;
        }

        // open a dummy client
        jack_status_t status;
        jack_client_t* client = juce::jack_client_open ("JuceJackDummy", JackNoStartServer, &status);

        if (client == 0)
        {
            dumpJackErrorMessage (status);
        }
        else
        {
            // scan for output devices
            const char** ports = juce::jack_get_ports (client, 0, 0, /* JackPortIsPhysical | */ JackPortIsOutput);

            if (ports != 0)
            {
                int j = 0;
                while (ports[j] != 0)
                {
                    String clientName (ports[j]);
                    clientName = clientName.upToFirstOccurrenceOf (":", false, false);

                    if (clientName != String (JUCE_JACK_CLIENT_NAME)
                         && ! inputNames.contains (clientName))
                    {
                        inputNames.add (clientName);
                        inputIds.add (ports [j]);
                    }

                    ++j;
                }

                free (ports);
            }

            // scan for input devices
            ports = juce::jack_get_ports (client, 0, 0, /* JackPortIsPhysical | */ JackPortIsInput);

            if (ports != 0)
            {
                int j = 0;
                while (ports[j] != 0)
                {
                    String clientName (ports[j]);
                    clientName = clientName.upToFirstOccurrenceOf (":", false, false);

                    if (clientName != String (JUCE_JACK_CLIENT_NAME)
                         && ! outputNames.contains (clientName))
                    {
                        outputNames.add (clientName);
                        outputIds.add (ports [j]);
                    }

                    ++j;
                }

                free (ports);
            }

            juce::jack_client_close (client);
        }
    }

    StringArray getDeviceNames (bool wantInputNames) const
    {
        jassert (hasScanned); // need to call scanForDevices() before doing this
        return wantInputNames ? inputNames : outputNames;
    }

    int getDefaultDeviceIndex (bool forInput) const
    {
        jassert (hasScanned); // need to call scanForDevices() before doing this
        return 0;
    }

    bool hasSeparateInputsAndOutputs() const    { return true; }

    int getIndexOfDevice (AudioIODevice* device, bool asInput) const
    {
        jassert (hasScanned); // need to call scanForDevices() before doing this

        JackAudioIODevice* d = dynamic_cast <JackAudioIODevice*> (device);
        if (d == 0)
            return -1;

        return asInput ? inputIds.indexOf (d->inputId)
                       : outputIds.indexOf (d->outputId);
    }

    AudioIODevice* createDevice (const String& outputDeviceName,
                                 const String& inputDeviceName)
    {
        jassert (hasScanned); // need to call scanForDevices() before doing this

        const int inputIndex = inputNames.indexOf (inputDeviceName);
        const int outputIndex = outputNames.indexOf (outputDeviceName);

        if (inputIndex >= 0 || outputIndex >= 0)
            return new JackAudioIODevice (outputIndex >= 0 ? outputDeviceName
                                                           : inputDeviceName,
                                          inputIds [inputIndex],
                                          outputIds [outputIndex]);

        return nullptr;
    }

    //==============================================================================
private:
    StringArray inputNames, outputNames, inputIds, outputIds;
    bool hasScanned;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JackAudioIODeviceType);
};

//==============================================================================
AudioIODeviceType* AudioIODeviceType::createAudioIODeviceType_JACK()
{
    return new JackAudioIODeviceType();
}
