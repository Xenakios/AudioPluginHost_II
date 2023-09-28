#pragma once

#include <clap/helpers/event-list.hh>
#include "containers/choc_SingleReaderSingleWriterFIFO.h"

void printClapEvents(clap::helpers::EventList &elist);

template<typename T>
class SingleReaderSingleWriterFifoHelper : public choc::fifo::SingleReaderSingleWriterFIFO<T>
{
public:
    SingleReaderSingleWriterFifoHelper(size_t initial_size = 2048)
    {
        choc::fifo::SingleReaderSingleWriterFIFO<T>::reset(initial_size);
    }
};
