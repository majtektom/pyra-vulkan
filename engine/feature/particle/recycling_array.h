#pragma once

namespace engine3d::particles {

template <class T>
class RecyclingArray {
public:
    RecyclingArray(int numElements);
    virtual ~RecyclingArray();
    T* New();
    bool Delete(int index);
    int GetNumFreeElements();
    int GetNumUsedElements();
    int GetTotalElements();
    T& GetAt(int index);
    bool IsAlive(int index);

protected:
    int m_iNumElements;
    int m_iUsedElements;
    T* m_aElements;
    bool* m_bAlive;
    int m_iLastNew;
    T m_NewTemplate;
};

template <class T>
RecyclingArray<T>::RecyclingArray(int numElements)
    : m_iNumElements(numElements) {
    m_bAlive = new bool[numElements];
    m_aElements = new T[numElements];
    for (int q = 0; q < numElements; q++)
        m_bAlive[q] = false;
    m_iUsedElements = 0;
    m_iLastNew = 0;
}

template <class T>
RecyclingArray<T>::~RecyclingArray() {
    delete[] m_bAlive;
    delete[] m_aElements;
}

template <class T>
T* RecyclingArray<T>::New() {
    if (GetNumFreeElements() < 1) return nullptr;
    int i = m_iLastNew;
    for (int q = 0; q < m_iNumElements; q++) {
        if (!m_bAlive[i]) break;
        i++;
        if (i >= m_iNumElements) i = 0;
    }
    if (m_bAlive[i]) return nullptr;
    m_aElements[i] = m_NewTemplate;
    m_iUsedElements++;
    m_bAlive[i] = true;
    m_iLastNew = i;
    return &m_aElements[i];
}

template <class T>
bool RecyclingArray<T>::Delete(int index) {
    if (index < 0 || index >= m_iNumElements || !m_bAlive[index])
        return false;
    m_bAlive[index] = false;
    m_iUsedElements--;
    return true;
}

template <class T>
int RecyclingArray<T>::GetNumFreeElements() { return m_iNumElements - GetNumUsedElements(); }
template <class T>
int RecyclingArray<T>::GetNumUsedElements() { return m_iUsedElements; }
template <class T>
int RecyclingArray<T>::GetTotalElements() { return m_iNumElements; }
template <class T>
T& RecyclingArray<T>::GetAt(int index) { return m_aElements[index]; }
template <class T>
bool RecyclingArray<T>::IsAlive(int index) { return m_bAlive[index]; }

} // namespace engine3d::particles
